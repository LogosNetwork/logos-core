/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/token/requests.hpp>
#include <logos/token/entry.hpp>
#include <logos/lib/trace.hpp>
#include <logos/common.hpp>

constexpr uint128_t PersistenceManager<R>::MIN_TRANSACTION_FEE;

PersistenceManager<R>::PersistenceManager(Store & store,
                                          ReservationsPtr reservations,
                                          Milliseconds clock_drift)
    : Persistence(store, clock_drift)
    , _reservations(reservations)
{
    if (_reservations == nullptr)
    {
        LOG_WARN(_log) << "PersistenceManager creating default reservations";
        _reservations = std::make_shared<DefaultReservations>(store);
    }
}

void PersistenceManager<R>::ApplyUpdates(const ApprovedRB & message,
                                         uint8_t delegate_id)
{
    // XXX - Failure during any of the database operations
    //       performed in the following two methods will cause
    //       the application to exit without committing the
    //       intermediate transactions to the database.

    auto batch_hash = message.Hash();
    uint16_t count = 0;
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        auto request = static_pointer_cast<const Send>(message.requests[i]);
        request->batch_hash = batch_hash;
        request->index_in_batch = count++;
    }

    LOG_DEBUG(_log) << "PersistenceManager<R>::ApplyUpdates - RequestBlock with "
                    << message.requests.size()
                    << " Requests";

    logos::transaction transaction(_store.environment, nullptr, true);
    StoreRequestBlock(message, transaction, delegate_id);
    ApplyRequestBlock(message, transaction);
}

bool PersistenceManager<R>::Validate(RequestPtr request,
                                     logos::process_return & result,
                                     bool allow_duplicates)
{
    auto hash = request->GetHash();

    std::lock_guard<std::mutex> lock(_reservation_mutex);

    std::shared_ptr<logos::Account> info;
    auto account_error(_reservations->Acquire(request->GetAccount(), info, request->GetAccountType()));

    // Account exists.
    if(!account_error)
    {
        if(info->block_count != request->sequence)
        {
            result.code = logos::process_result::wrong_sequence_number;
            LOG_INFO(_log) << "wrong_sequence_number, request sqn=" << request->sequence
                           << " expecting=" << info->block_count;
            return false;
        }

        // No previous hash set.
        if(request->previous.is_zero() && info->block_count)
        {
            result.code = logos::process_result::fork;
            return false;
        }

        // This account has issued at
        // least one request.
        if(info->block_count)
        {
            if(!_store.request_exists(request->previous))
            {
                result.code = logos::process_result::gap_previous;
                BOOST_LOG (_log) << "GAP_PREVIOUS: cannot find previous hash "
                                 << request->previous.to_string()
                                 << "; current account info head is: "
                                 << info->head.to_string();

                return false;
            }
        }

        if(request->previous != info->head)
        {
            // Allow duplicate requests (hash == info->head)
            // received from request blocks sent by other
            // delegates in PrePrepares.
            if(hash == info->head)
            {
                if(allow_duplicates)
                {
                    result.code = logos::process_result::progress;
                    return true;
                }
                else
                {
                    result.code = logos::process_result::old;
                    return false;
                }
            }
            else
            {
                result.code = logos::process_result::fork;
                return false;
            }
        }

        // Have we seen this request before?
        if(_store.request_exists(hash))
        {
            result.code = logos::process_result::old;
            return false;
        }

        // TODO
        uint32_t current_epoch = 0;

        auto update_reservation = [&info, &hash, current_epoch]()
                                  {
                                       info->reservation = hash;
                                       info->reservation_epoch = current_epoch;
                                  };

        // Account is not reserved.
        if(info->reservation.is_zero())
        {
            update_reservation();
        }

        // Account is already reserved.
        else if(info->reservation != hash)
        {
            // This request conflicts with an
            // existing reservation.
            if(current_epoch < info->reservation_epoch + RESERVATION_PERIOD)
            {
                result.code = logos::process_result::already_reserved;
                return false;
            }

            // Reservation has expired.
            update_reservation();
        }

        // Make sure there's enough Logos
        // to cover the request.
        if(request->GetLogosTotal() > info->balance)
        {
            result.code = logos::process_result::insufficient_balance;
            return false;
        }

        // Only controllers can issue these
        if(IsTokenAdminRequest(request->type))
        {
            auto token_account = std::static_pointer_cast<TokenAccount>(info);

            auto entry = std::find_if(token_account->controllers.begin(),
                                      token_account->controllers.end(),
                                      [request](const ControllerInfo & c)
                                      {
                                          return c.account == request->origin;
                                      });

            // The sender isn't a controller
            if(entry == token_account->controllers.end())
            {
                result.code = logos::process_result::unauthorized_request;
                return false;
            }

            // The controller isn't authorized
            // to make this request.
            if(!entry->IsAuthorized(request))
            {
                result.code = logos::process_result::unauthorized_request;
                return false;
            }

            if(!token_account->IsAllowed(request))
            {
                result.code = logos::process_result::prohibitted_request;
                return false;
            }
        }

        uint16_t token_total = request->GetTokenTotal();

        // This request transfers tokens
        if(token_total > 0)
        {
            // The account that will own the request
            // is not the account losing/sending
            // tokens.
            if(request->GetAccount() != request->GetSource())
            {
                std::shared_ptr<logos::Account> source;
                if(_store.account_get(request->GetSource(), source))
                {
                    // TODO: Bootstrapping
                    result.code = logos::process_result::unknown_source_account;
                    return false;
                }

                // The available tokens and the
                // amount requested don't add up.
                if(!request->Validate(result, source))
                {
                    return false;
                }
                else
                {
                    // TODO: Pending revoke cache
                }
            }

            // The account that will own the request
            // is the account losing/sending tokens.
            else
            {

                // This is a Send Token Request.
                if(request->type == RequestType::SendTokens)
                {
                    auto send_tokens = dynamic_pointer_cast<const TokenSend>(request);
                    assert(send_tokens);

                    std::shared_ptr<TokenAccount> token_account;

                    // This token id doesn't exist.
                    if(_reservations->Acquire(send_tokens->token_id, token_account))
                    {
                        result.code = logos::process_result::invalid_token_id;
                        return false;
                    }

                    auto user_account = std::static_pointer_cast<logos::account_info>(info);

                    // Get sender's token entry
                    TokenEntry source_token_entry;
                    if(!user_account->GetEntry(send_tokens->token_id, source_token_entry))
                    {
                        result.code = logos::process_result::untethered_account;
                        return false;
                    }

                    // The sender's account is either frozen or
                    // not yet whitelisted.
                    if(!token_account->SendAllowed(source_token_entry.status, result))
                    {
                        return false;
                    }

                    // Check each transaction in the Send Token Request
                    for(auto & t : send_tokens->transactions)
                    {
                        std::shared_ptr<logos::account_info> destination;
                        TokenUserStatus destination_status;

                        BlockHash token_user_id(GetTokenUserId(send_tokens->token_id,
                                                               t.destination));

                        // We have the destination account
                        if(!_reservations->Acquire(t.destination, destination))
                        {
                            TokenEntry destination_token_entry;

                            // This destinationaccount has been tethered to
                            // the token
                            if(destination->GetEntry(send_tokens->token_id, destination_token_entry))
                            {
                                destination_status = destination_token_entry.status;
                            }

                            // This destination account is untethered
                            else
                            {
                                _store.token_user_status_get(token_user_id, destination_status);
                            }
                        }

                        // We don't have the destination account
                        else
                        {
                            _store.token_user_status_get(token_user_id, destination_status);
                        }

                        // The destination account is either frozen
                        // or not yet whitelisted.
                        if(!token_account->SendAllowed(destination_status, result))
                        {
                            return false;
                        }

                        // Token fee is insufficient
                        if(!token_account->FeeSufficient(token_total, send_tokens->token_fee))
                        {
                            result.code = logos::process_result::insufficient_token_fee;
                            return false;
                        }
                    }
                }

                if(!request->Validate(result, info))
                {
                    return false;
                }
            }
        }
    }

    // Account doesn't exist
    else
    {
        // Currently do not accept requests
        // with non-existent accounts.
        result.code = logos::process_result::unknown_source_account;
        return false;
    }

    result.code = logos::process_result::progress;
    return true;
}

bool PersistenceManager<R>::Validate(RequestPtr request)
{
    logos::process_return ignored_result;
    auto val = Validate(request, ignored_result);

    LOG_DEBUG(_log) << "PersistenceManager<R>::Validate code "
                    << (uint)ignored_result.code;

    return val;
}

bool PersistenceManager<R>::Validate(const PrePrepare & message,
                                     ValidationStatus * status)
{
    using namespace logos;

    bool valid = true;
    for(uint64_t i = 0; i < message.requests.size(); ++i)
    {
        logos::process_return result;
        if(!Validate(message.requests[i], result))
        {
            UpdateStatusRequests(status, i, result.code);
            UpdateStatusReason(status, process_result::invalid_request);

            valid = false;
        }
    }

    return valid;
}

void PersistenceManager<R>::StoreRequestBlock(const ApprovedRB & message,
                                              MDB_txn * transaction,
                                              uint8_t delegate_id)
{
    auto hash(message.Hash());
    LOG_DEBUG(_log) << "PersistenceManager::StoreRequestBlock - "
                    << message.Hash().to_string();

    if(_store.request_block_put(message, hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::StoreRequestBlock - "
                        << "Failed to store batch message with hash: "
                        << hash.to_string();

        trace_and_halt();
    }

    if(_store.request_tip_put(delegate_id, hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::StoreRequestBlock - "
                        << "Failed to store batch block tip with hash: "
                        << hash.to_string();

        trace_and_halt();
    }

    if(! message.previous.is_zero())
    {
        if(_store.consensus_block_update_next(message.previous, hash, ConsensusType::Request, transaction))
        {
            // TODO: bootstrap here.
        }
    }

    // TODO: Add previous hash for request blocks with
    //       a previous set to zero because it was
    //       the first batch of the epoch.
}

void PersistenceManager<R>::ApplyRequestBlock(
    const ApprovedRB & message,
    MDB_txn * transaction)
{
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        ApplyRequest(message.requests[i],
                     message.timestamp,
                     transaction);

        std::lock_guard<std::mutex> lock(_reservation_mutex);
        _reservations->Release(message.requests[i]->GetAccount());
    }
}

void PersistenceManager<R>::ApplyRequest(RequestPtr request,
                                         uint64_t timestamp,
                                         MDB_txn * transaction)
{
    std::shared_ptr<logos::Account> info;
    auto account_error(_store.account_get(request->origin, info, request->GetAccountType()));

    if(account_error)
    {
        LOG_ERROR (_log) << "PersistenceManager::ApplyRequest - Unable to find account.";
        return;
    }

    // This can happen when a duplicate request
    // is accepted. We can ignore this transaction.
    if(request->previous != info->head)
    {
        LOG_INFO(_log) << "Block previous ("
                       << request->previous.to_string()
                       << ") does not match account head ("
                       << info->head.to_string()
                       << "). Suspected duplicate request - "
                       << "ignoring.";
        return;
    }


    info->block_count++;
    info->head = request->GetHash();
    info->modified = logos::seconds_since_epoch();

    // TODO: Harvest fees
    info->balance -= MIN_TRANSACTION_FEE;

    enum class Status : uint8_t
    {
        FROZEN      = 0,
        UNFROZEN    = 1,
        WHITELISTED = 2
    };

    // Performs the actions required by whitelisting
    // and freezing.
    auto update_token_user_status = [this, &transaction](auto message, Status status)
    {

        // Set the appropriate field
        // according to the required
        // status change.
        auto do_update_status = [status](TokenUserStatus & user_status)
        {
            switch(status)
            {
                case Status::FROZEN:
                    user_status.frozen = true;
                    break;
                case Status::UNFROZEN:
                    user_status.frozen = false;
                    break;
                case Status::WHITELISTED:
                    user_status.whitelisted = true;
                    break;
            }
        };

        // Update the user's status and persist
        // the change.
        auto update_status = [this, &message, &transaction, status, &do_update_status]()
        {
            TokenUserStatus user_status;
            _store.token_user_status_get(message->token_id, user_status, transaction);

            do_update_status(user_status);

            _store.token_user_status_put(message->token_id, user_status, transaction);
        };

        logos::account_info user_account;

        // Account was found
        if(!_store.account_get(message->account, user_account, transaction))
        {
            auto entry = user_account.GetEntry(message->token_id);

            // Account is tethered; use TokenEntry
            if(entry != user_account.entries.end())
            {
                do_update_status(entry->status);
                if(_store.account_put(message->account, user_account, transaction))
                {
                    // TODO: log
                }
            }

            // Account is untethered; use the
            // central freeze/whitelist
            else
            {
                update_status();
            }
        }

        // Account was not found; use the
        // central freeze/whitelist
        else
        {
            update_status();
        }

    };

    switch(request->type)
    {
        case RequestType::Send:
        {
            auto send = static_pointer_cast<const Send>(request);
            auto source = static_pointer_cast<logos::account_info>(info);

            source->balance -= send->GetLogosTotal() - send->fee;

            ApplySend(send,
                      timestamp,
                      transaction);
            break;
        }
        case RequestType::ChangeRep:
            break;
        case RequestType::IssueTokens:
        {
            auto issuance = static_pointer_cast<const TokenIssuance>(request);
            TokenAccount account(*issuance);

            account.settings = issuance->settings;
            account.controllers = issuance->controllers;
            break;
        }
        case RequestType::IssueAdtlTokens:
        {
            auto issue_adtl = static_pointer_cast<const TokenIssueAdtl>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            token_account->token_balance += issue_adtl->amount;
            break;
        }
        case RequestType::ChangeTokenSetting:
        {
            auto change = static_pointer_cast<const TokenChangeSetting>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            token_account->Set(change->setting, change->value);
            break;
        }
        case RequestType::ImmuteTokenSetting:
        {
            auto immute = static_pointer_cast<const TokenImmuteSetting>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            token_account->Set(token_account->GetMutabilitySetting(immute->setting), false);
            break;
        }
        case RequestType::RevokeTokens:
        {
            auto revoke = static_pointer_cast<const TokenRevoke>(request);

            logos::account_info user_account;

            // TODO: Pending revoke cache
            if(!_store.account_get(revoke->source, user_account, transaction))
            {
                auto entry = user_account.GetEntry(revoke->token_id);
                assert(entry != user_account.entries.end());

                entry->balance -= revoke->transaction.amount;

                if(_store.account_put(revoke->source, user_account, transaction))
                {
                    // TODO: Log
                }
            }

            // Couldn't find account
            else
            {
                // TODO: Log
            }

            ApplySend(revoke->transaction,
                      timestamp,
                      transaction,
                      revoke->token_id,
                      revoke->GetHash());

            break;
        }
        case RequestType::FreezeTokens:
        {
            auto freeze = static_pointer_cast<const TokenFreeze>(request);
            update_token_user_status(freeze,
                                     freeze->action == FreezeAction::Freeze ?
                                     Status::FROZEN : Status::UNFROZEN);
            break;
        }
        case RequestType::SetTokenFee:
        {
            auto set_fee = static_pointer_cast<const TokenSetFee>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            token_account->fee_type = set_fee->fee_type;
            token_account->fee_rate = set_fee->fee_rate;

            break;
        }
        case RequestType::UpdateWhitelist:
        {
            auto whitelist = static_pointer_cast<const TokenWhitelist>(request);
            update_token_user_status(whitelist, Status::WHITELISTED);
            break;
        }
        case RequestType::UpdateIssuerInfo:
        {
            auto issuer_info = static_pointer_cast<const TokenIssuerInfo>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            token_account->issuer_info = issuer_info->new_info;

            break;
        }
        case RequestType::UpdateController:
        {
            auto update = static_pointer_cast<const TokenController>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            if(update->action == ControllerAction::Add)
            {
                token_account->controllers.push_back(update->controller);
            }
            else
            {
                auto entry = std::find_if(token_account->controllers.begin(),
                                          token_account->controllers.end(),
                                          [update](const ControllerInfo & c)
                                          {
                                              return c.account == update->controller.account;
                                          });

                assert(entry != token_account->controllers.end());
                token_account->controllers.erase(entry);
            }

            break;
        }
        case RequestType::BurnTokens:
        {
            auto burn = static_pointer_cast<const TokenBurn>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            token_account->token_balance -= burn->amount;

            break;
        }
        case RequestType::DistributeTokens:
        {
            auto distribute = static_pointer_cast<const TokenAccountSend>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            token_account->token_balance -= distribute->transaction.amount;

            ApplySend(distribute->transaction,
                      timestamp,
                      transaction,
                      distribute->GetHash(),
                      distribute->token_id);

            break;
        }
        case RequestType::WithdrawTokens:
        {
            auto withdraw = static_pointer_cast<const TokenAccountWithdrawFee>(request);
            auto token_account = static_pointer_cast<TokenAccount>(info);

            token_account->token_fee_balance -= withdraw->transaction.amount;

            ApplySend(withdraw->transaction,
                      timestamp,
                      transaction,
                      withdraw->GetHash(),
                      withdraw->token_id);

            break;
        }
        case RequestType::SendTokens:
        {
            auto send = static_pointer_cast<const TokenSend>(request);
            auto source = static_pointer_cast<logos::account_info>(info);

            auto entry = source->GetEntry(send->token_id);
            assert(entry != source->entries.end());

            entry->balance -= send->GetTokenTotal();

            ApplySend(send,
                      timestamp,
                      transaction,
                      send->token_id);

            break;
        }
        case RequestType::Unknown:
            // TODO
            break;
    }

    if(_store.account_put(request->GetAccount(), info, request->GetAccountType(), transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplyRequest - "
                        << "Failed to store account: "
                        << request->origin.to_string();

        std::exit(EXIT_FAILURE);
    }

}

template<typename SendType>
void PersistenceManager<R>::ApplySend(std::shared_ptr<const SendType> request,
                                      uint64_t timestamp,
                                      MDB_txn *transaction,
                                      BlockHash token_id)
{
    // Protects against a race condition concerning
    // simultaneous receives for the same account.
    //
    std::lock_guard<std::mutex> lock(_destination_mutex);
    uint16_t transaction_index = 0;
    for(auto & t : request->transactions)
    {
        ApplySend(t,
                  timestamp,
                  transaction,
                  request->GetHash(),
                  token_id,
                  transaction_index++);
    }
}

template<typename AmountType>
void PersistenceManager<R>::ApplySend(const Transaction<AmountType> &send,
                                      uint64_t timestamp,
                                      MDB_txn *transaction,
                                      const BlockHash &request_hash,
                                      const BlockHash &token_id,
                                      uint16_t transaction_index)
{
    logos::account_info info;
    auto account_error(_store.account_get(send.destination, info, transaction));

    ReceiveBlock receive(
        /* Previous          */ info.receive_head,
        /* send_hash         */ request_hash,
        /* transaction_index */ transaction_index
    );

    auto hash(receive.Hash());

    // Destination account doesn't exist yet
    if(account_error)
    {
        info.open_block = hash;
        LOG_DEBUG(_log) << "PersistenceManager::ApplySend - "
                        << "new account: "
                        << send.destination.to_string();
    }

    info.receive_count++;
    info.receive_head = hash;
    info.modified = logos::seconds_since_epoch();

    if(token_id.is_zero())
    {
        info.balance += send.amount;
    }
    else
    {
        auto entry = info.GetEntry(token_id);
        assert(entry != info.entries.end());

        // TODO: create explicit bond between
        //       token transactions and amount
        //       type.
        auto token_send = reinterpret_cast<const Transaction<uint16_t> &>(send);

        entry->balance += token_send.amount;
    }

    if(_store.account_put(send.destination, info, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                        << "Failed to store account: "
                        << send.destination.to_string();

        std::exit(EXIT_FAILURE);
    }

    PlaceReceive(receive, timestamp, transaction);
}

// TODO: Discuss total order of receives in
//       receive_db of all nodes.
void PersistenceManager<R>::PlaceReceive(ReceiveBlock & receive,
                                         uint64_t timestamp,
                                         MDB_txn * transaction)
{
    ReceiveBlock prev;
    ReceiveBlock cur;

    auto hash = receive.Hash();
    uint64_t timestamp_a = timestamp;

    if(!_store.receive_get(receive.previous, cur, transaction))
    {
        // Returns true if 'a' should precede 'b'
        // in the receive chain.
        auto receive_cmp = [&](const ReceiveBlock & a,
                               const ReceiveBlock & b)
        {
            // need b's timestamp
            Send send;
            if(!_store.request_get(b.send_hash, send, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                                << "Failed to get a previous state block with hash: "
                                << b.send_hash.to_string();
                trace_and_halt();
            }

            ApprovedRB approved;
            if(!_store.request_block_get(send.batch_hash, approved, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                                << "Failed to get a previous batch state block with hash: "
                                << send.batch_hash.to_string();
                trace_and_halt();
            }

            auto timestamp_b = approved.timestamp;
            bool a_is_less;
            if(timestamp_a != timestamp_b)
            {
                a_is_less = timestamp_a < timestamp_b;
            }
            else
            {
                a_is_less = a.Hash() < b.Hash();
            }

            // update for next compare if needed
            timestamp_a = timestamp_b;

            return a_is_less;
        };

        while(receive_cmp(receive, cur))
        {
            prev = cur;
            if(!_store.receive_get(cur.previous,
                                   cur,
                                   transaction))
            {
                break;
            }
        }

        Send prev_send;
        if(!_store.request_get(prev.send_hash, prev_send, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                            << "Failed to get a previous state block with hash: "
                            << prev.send_hash.to_string();
            trace_and_halt();
        }
        if(!prev_send.origin.is_zero())
        {
            receive.previous = prev.previous;
            prev.previous = hash;
        }
    }

    if(_store.receive_put(hash, receive, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                        << "Failed to store receive block with hash: "
                        << hash.to_string();

        std::exit(EXIT_FAILURE);
    }
}
