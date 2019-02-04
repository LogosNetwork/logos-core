/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/token/requests.hpp>
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

void PersistenceManager<R>::ApplyUpdates(
    const ApprovedRB & message,
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
    StoreBatchMessage(message, transaction, delegate_id);
    ApplyBatchMessage(message, transaction);
}

bool PersistenceManager<R>::Validate(
    std::shared_ptr<const Request> request,
    logos::process_return & result,
    bool allow_duplicates)
{
    auto hash = request->GetHash();

    std::lock_guard<std::mutex> lock(_reservation_mutex);

    std::shared_ptr<logos::Account> info;
    auto account_error(_reservations->Acquire(request->GetAccount(), info));

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

        // This request transfers tokens
        if(request->GetTokenTotal() > 0)
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

bool PersistenceManager<R>::Validate(
    std::shared_ptr<const Request> request)
{
    logos::process_return ignored_result;
    auto val = Validate(request, ignored_result);

    LOG_DEBUG(_log) << "PersistenceManager<R>::Validate code "
                    << (uint)ignored_result.code;

    return val;
}

bool PersistenceManager<R>::Validate(
    const PrePrepare & message,
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

void PersistenceManager<R>::StoreBatchMessage(
    const ApprovedRB & message,
    MDB_txn * transaction,
    uint8_t delegate_id)
{
    auto hash(message.Hash());
    LOG_DEBUG(_log) << "PersistenceManager::StoreBatchMessage - "
                    << message.Hash().to_string();

    if(_store.request_block_put(message, hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::StoreBatchMessage - "
                        << "Failed to store batch message with hash: "
                        << hash.to_string();

        trace_and_halt();
    }

    if(_store.batch_tip_put(delegate_id, hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::StoreBatchMessage - "
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

void PersistenceManager<R>::ApplyBatchMessage(
    const ApprovedRB & message,
    MDB_txn * transaction)
{
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        auto request = static_pointer_cast<Send>(message.requests[i]);

        ApplyStateMessage(*request,
                          message.timestamp,
                          transaction);

        std::lock_guard<std::mutex> lock(_reservation_mutex);
        _reservations->Release(
            static_pointer_cast<const Send>(message.requests[i])->account);
    }
}

// Currently designed only to handle
// send transactions.
void PersistenceManager<R>::ApplyStateMessage(
    const Send & request,
    uint64_t timestamp,
    MDB_txn * transaction)
{
    if(!UpdateSourceState(request, transaction))
    {
        UpdateDestinationState(request, timestamp, transaction);
    }
}

bool PersistenceManager<R>::UpdateSourceState(
    const Send & request,
    MDB_txn * transaction)
{
    logos::account_info info;
    auto account_error(_store.account_get(request.account, info));

    if(account_error)
    {
        LOG_ERROR (_log) << "PersistenceManager::UpdateSourceState - Unable to find account.";
        return true;
    }

    // This can happen when a duplicate request
    // is accepted. We can ignore this transaction.
    if(request.previous != info.head)
    {
        LOG_INFO(_log) << "Block previous ("
                       << request.previous.to_string()
                       << ") does not match account head ("
                       << info.head.to_string()
                       << "). Suspected duplicate request - "
                       << "ignoring.";
        return true;
    }

    info.block_count++;
    info.balance = info.balance.number() -
                   request.fee.number();

    for(auto & t : request.transactions)
    {
        info.balance = info.balance.number() - t.amount.number();
    }

    info.head = request.GetHash();
    info.modified = logos::seconds_since_epoch();

    if(_store.account_put(request.account, info, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::UpdateSourceState - "
                        << "Failed to store account: "
                        << request.account.to_string();

        std::exit(EXIT_FAILURE);
    }

    return false;
}

void PersistenceManager<R>::UpdateDestinationState(
    const Send & request,
    uint64_t timestamp,
    MDB_txn * transaction)
{
    // Protects against a race condition concerning
    // simultaneous receives for the same account.
    //
    std::lock_guard<std::mutex> lock(_destination_mutex);
    uint16_t index2send = 0;
    for(auto & t : request.transactions)
    {
        logos::account_info info;
        auto account_error(_store.account_get(t.destination, info));

        ReceiveBlock receive(
                /* Previous   */ info.receive_head,
                /* send_hash  */ request.GetHash(),
                /* index2send */ index2send++
        );

        auto hash(receive.Hash());

        // Destination account doesn't exist yet
        if(account_error)
        {
            info.open_block = hash;
            LOG_DEBUG(_log) << "PersistenceManager::UpdateDestinationState - "
                            << "new account: "
                            << t.destination.to_string();
        }

        info.receive_count++;
        info.receive_head = hash;
        info.balance = info.balance.number() + t.amount.number();
        info.modified = logos::seconds_since_epoch();

        if(_store.account_put(t.destination, info, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
                            << "Failed to store account: "
                            << t.destination.to_string();

            std::exit(EXIT_FAILURE);
        }

        PlaceReceive(receive, timestamp, transaction);
    }
}

// TODO: Discuss total order of receives in
//       receive_db of all nodes.
void PersistenceManager<R>::PlaceReceive(
    ReceiveBlock & receive,
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
                LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
                                << "Failed to get a previous state block with hash: "
                                << b.send_hash.to_string();
                trace_and_halt();
            }

            ApprovedRB approved;
            if(!_store.request_block_get(send.batch_hash, approved, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
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
            LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
                            << "Failed to get a previous state block with hash: "
                            << prev.send_hash.to_string();
            trace_and_halt();
        }
        if(!prev_send.account.is_zero())
        {
            receive.previous = prev.previous;
            prev.previous = hash;
        }
    }

    if(_store.receive_put(hash, receive, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
                        << "Failed to store receive block with hash: "
                        << hash.to_string();

        std::exit(EXIT_FAILURE);
    }
}
