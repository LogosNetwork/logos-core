/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/token/requests.hpp>
#include <logos/token/entry.hpp>
#include <logos/lib/trace.hpp>
#include <logos/common.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>

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
        _reservations = std::make_shared<Reservations>(store);
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

    // SYL Integration: Temporary fix (same for epochs and micro blocks):
    // Check if block exists again here to avoid situations where P2P receives a Post_Commit,
    // doesn't think the block exists, but then direct consensus persists the block, and P2P tries to persist again.
    // Ultimately we want to use the same global queue for direct consensus, P2P, and bootstrapping.

    if (BlockExists(message))
    {
        LOG_DEBUG(_log) << "PersistenceManager<R>::ApplyUpdates - request block already exists, ignoring";
        return;
    }

    uint16_t count = 0;
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        auto request = message.requests[i];
        request->locator.hash = batch_hash;
        request->locator.index = count++;
    }

    LOG_DEBUG(_log) << "PersistenceManager<R>::ApplyUpdates - RequestBlock with "
                    << message.requests.size()
                    << " Requests";

    // SYL integration: need to ensure the operations below execute atomically
    // Otherwise, multiple calls to batch persistence may overwrite balance for the same account
    std::lock_guard<std::mutex> lock (_write_mutex);
    {
        logos::transaction transaction(_store.environment, nullptr, true);
        StoreRequestBlock(message, transaction, delegate_id);
        ApplyRequestBlock(message, transaction);
    }
    // SYL Integration: clear reservation AFTER flushing to LMDB to ensure safety
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        _reservations->Release(message.requests[i]->GetAccount());
    }
}

bool PersistenceManager<R>::BlockExists(
    const ApprovedRB & message)
{
    return _store.request_block_exists(message);
}

bool PersistenceManager<R>::ValidateRequest(
    RequestPtr request,
    uint32_t cur_epoch_num,
    logos::process_return & result,
    bool allow_duplicates,
    bool prelim)
{
    LOG_INFO(_log) << "PersistenceManager::ValidateRequest - validating request"
        << request->Hash().to_string();
    // SYL Integration: move signature validation here so we always check
    if(ConsensusContainer::ValidateSigConfig() && ! request->VerifySignature(request->origin))
    {
        LOG_WARN(_log) << "PersistenceManager<R> - Validate, bad signature: "
            << request->signature.to_string()
            << " account: " << request->origin.to_string();

        result.code = logos::process_result::bad_signature;
        return false;
    }

    if(!request->Validate(result))
    {
        return false;
    }

    auto hash = request->GetHash();

    if(!_store.account_exists(request->origin))
    {
        result.code = logos::process_result::unknown_origin;
        return false;
    }

    // burn account and transaction fee validation is done in TxAcceptor
    // SYL Integration: remove _reservation_mutex for now and rely on coarser _write_mutex. Potential fix later

    std::shared_ptr<logos::Account> info;

    // The account doesn't exist
    if (_store.account_get(request->GetAccount(), info))
    {
        // We can only get here if this is an administrative
        // token request, which means an invalid token ID
        // was provided.
        result.code = logos::process_result::invalid_token_id;
        return false;
    }

    // a valid (non-expired) reservation exits
    if (!_reservations->CanAcquire(request->GetAccount(), hash, allow_duplicates))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate - Account already reserved! ";
        result.code = logos::process_result::already_reserved;
        return false;
    }

    // Set prelim to true single transaction (non-batch) validation from TxAcceptor, false for RPC
    if (prelim)
    {
        result.code = logos::process_result::progress;
        return true;
    }

    // Move on to check account info
    //sequence number
    if(info->block_count != request->sequence)
    {
        result.code = logos::process_result::wrong_sequence_number;
        LOG_INFO(_log) << "wrong_sequence_number, request sqn=" << request->sequence
            << " expecting=" << info->block_count;
        return false;
    }

    // No previous block set.
    if(request->previous.is_zero() && info->block_count)
    {
        result.code = logos::process_result::fork;
        return false;
    }

    // This account has issued at least one send transaction.
    if(info->block_count)
    {
        if(!_store.request_exists(request->previous))
        {
            result.code = logos::process_result::gap_previous;
            LOG_WARN (_log) << "GAP_PREVIOUS: cannot find previous hash " << request->previous.to_string()
                << "; current account info head is: " << info->head.to_string();
            return false;
        }
    }

    if(request->previous != info->head)
    {
        LOG_WARN (_log) << "PersistenceManager::Validate - discrepancy between block previous hash ("
            << request->previous.to_string()
            << ") and current account info head ("
            << info->head.to_string() << ")";

        // Allow duplicate requests (either hash == info.head or hash matches a transaction further up in the chain)
        // received from batch blocks.
        if(hash == info->head || _store.request_exists(hash))
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

    // Make sure there's enough Logos
    // to cover the request.
    if(request->GetLogosTotal() > info->balance)
    {
        result.code = logos::process_result::insufficient_balance;
        return false;
    }

    switch(request->type)
    {
        case RequestType::Send:
        case RequestType::Change:
            break;
        case RequestType::Issuance:
        {
            auto issuance = dynamic_pointer_cast<const Issuance>(request);

            if(!issuance)
            {
                result.code = logos::process_result::invalid_request;
                return false;
            }

            if(_store.account_exists(issuance->token_id))
            {
                result.code = logos::process_result::key_collision;
                return false;
            }

            break;
        }
        case RequestType::ChangeSetting:
        case RequestType::IssueAdditional:
        case RequestType::ImmuteSetting:
        case RequestType::Revoke:
        case RequestType::AdjustUserStatus:
        case RequestType::AdjustFee:
        case RequestType::UpdateIssuerInfo:
        case RequestType::UpdateController:
        case RequestType::Burn:
        case RequestType::Distribute:
        case RequestType::WithdrawFee:
        case RequestType::WithdrawLogos:
            if(!ValidateTokenAdminRequest(request, result, info))
            {
                return false;
            }
        case RequestType::TokenSend:
            if(!ValidateTokenTransfer(request, result, info, request->GetTokenTotal()))
            {
                return false;
            }
            break;
        case RequestType::ElectionVote:
        {
            logos::transaction txn(_store.environment,nullptr,false);
            auto ev = dynamic_pointer_cast<const ElectionVote>(request);
            if(!ValidateRequest(*ev, cur_epoch_num, txn, result))
            {
                LOG_ERROR(_log) << "ElectionVote is invalid: " << ev->Hash().to_string()
                << " code is " << logos::ProcessResultToString(result.code);
                return false;
            }
            break;
        }
        case RequestType::AnnounceCandidacy:
        {
            logos::transaction txn(_store.environment,nullptr,false);
            auto ac = dynamic_pointer_cast<const AnnounceCandidacy>(request);
            if(!ValidateRequest(*ac, cur_epoch_num, txn, result))
            {
                LOG_ERROR(_log) << "AnnounceCandidacy is invalid: " << ac->Hash().to_string()
                << " code is " << logos::ProcessResultToString(result.code);
                return false;
            }
            break;
        }
        case RequestType::RenounceCandidacy:
        {
            logos::transaction txn(_store.environment,nullptr,false);
            auto rc = dynamic_pointer_cast<const RenounceCandidacy>(request);
            if(!ValidateRequest(*rc,cur_epoch_num,txn, result))
            {
                LOG_ERROR(_log) << "RenounceCandidacy is invalid: " << rc->Hash().to_string()
                << " code is " << logos::ProcessResultToString(result.code);
                return false;
            }
            break;
        }
        case RequestType::StartRepresenting:
        {
            logos::transaction txn(_store.environment,nullptr,false);
            auto sr = dynamic_pointer_cast<const StartRepresenting>(request);
            if(!ValidateRequest(*sr,cur_epoch_num,txn, result))
            {
                LOG_ERROR(_log) << "StartRepresenting is invalid: " << sr->Hash().to_string()
                << " code is " << logos::ProcessResultToString(result.code);
                return false;
            }
            break;
        }
        case RequestType::StopRepresenting:
        {
            logos::transaction txn(_store.environment,nullptr,false);
            auto sr = dynamic_pointer_cast<const StopRepresenting>(request);
            if(!ValidateRequest(*sr,cur_epoch_num,txn,result))
            {
                LOG_ERROR(_log) << "StartRepresenting is invalid: " << sr->Hash().to_string()
                << " code is " << logos::ProcessResultToString(result.code);
                return false;
            }   
            break;
        }
        case RequestType::Unknown:
            LOG_ERROR(_log) << "PersistenceManager::Validate - Received unknown request type";

            result.code = logos::process_result::invalid_request;
            return false;

            break;
    }

    result.code = logos::process_result::progress;
    return true;
}

// Use this for single transaction (non-batch) validation from RPC
bool PersistenceManager<R>::ValidateSingleRequest(
        const RequestPtr request, uint32_t cur_epoch_num, logos::process_return & result, bool allow_duplicates)
{
    std::lock_guard<std::mutex> lock(_write_mutex);
    return ValidateRequest(request, cur_epoch_num, result, allow_duplicates, false);
}

// Use this for batched transactions validation (either PrepareNextBatch or backup validation)
bool PersistenceManager<R>::ValidateAndUpdate(
    RequestPtr request, uint32_t cur_epoch_num, logos::process_return & result, bool allow_duplicates)
{
    auto success (ValidateRequest(request, cur_epoch_num, result, allow_duplicates, false));

    LOG_INFO(_log) << "PersistenceManager::ValidateAndUpdate - "
        << "request is : " << request->Hash().to_string() <<  " . result is "
        << success;
    if (success)
    {
        _reservations->UpdateReservation(request->GetHash(), request->GetAccount());
    }
    return success;
}

bool PersistenceManager<R>::ValidateBatch(
    const PrePrepare & message, RejectionMap & rejection_map)
{
    // SYL Integration: use _write_mutex because we have to wait for other database writes to finish flushing
    bool valid = true;
    logos::process_return ignored_result;
    std::lock_guard<std::mutex> lock (_write_mutex);
    for(uint64_t i = 0; i < message.requests.size(); ++i)
    {
#ifdef TEST_REJECT
        if(!ValidateAndUpdate(message.requests[i], message.epoch_number, ignored_result, true) || bool(message.requests[i].hash().number() & 1))
#else
        if(!ValidateAndUpdate(message.requests[i], message.epoch_number, ignored_result, true))
#endif
        {
            LOG_WARN(_log) << "PersistenceManager<R>::Validate - Rejecting " << message.requests[i]->GetHash().to_string();
            rejection_map[i] = true;

            if(valid)
            {
                valid = false;
            }
        }
    }
    return valid;
}

bool PersistenceManager<R>::Validate(const PrePrepare & message,
                                     ValidationStatus * status)
{
    using namespace logos;

    bool valid = true;
    std::lock_guard<std::mutex> lock (_write_mutex);
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        logos::process_return   result;
        LOG_INFO(_log) << "PersistenceManager::Validate - " 
            << "attempting to validate : " << message.requests[i]->Hash().to_string();
        if(!ValidateRequest(message.requests[i], message.epoch_number, result, true, false))
        {
            UpdateStatusRequests(status, i, result.code);
            UpdateStatusReason(status, process_result::invalid_request);
            LOG_INFO(_log) << "PersistenceManager::Validate - "
                << "failed to validate request : " << message.requests[i]->Hash().to_string(); 

            valid = false;
        }
    }

    return valid;
}

bool PersistenceManager<R>::ValidateTokenAdminRequest(RequestPtr request,
                                                      logos::process_return & result,
                                                      std::shared_ptr<logos::Account> info)
{
    auto token_account = dynamic_pointer_cast<TokenAccount>(info);

    if(!token_account)
    {
        result.code = logos::process_result::invalid_request;
        return false;
    }

    ControllerInfo controller;

    // The sender isn't a controller
    if (!token_account->GetController(request->origin, controller))
    {
        result.code = logos::process_result::unauthorized_request;
        return false;
    }

    // The controller isn't authorized
    // to make this request.
    if (!controller.IsAuthorized(request))
    {
        result.code = logos::process_result::unauthorized_request;
        return false;
    }

    // The accounts settings prohibit
    // this request
    if (!token_account->IsAllowed(request))
    {
        result.code = logos::process_result::prohibitted_request;
        return false;
    }

    if(request->type == RequestType::Revoke)
    {
        return true;
    }

    return request->Validate(result, info);
}

bool PersistenceManager<R>::ValidateTokenTransfer(RequestPtr request,
                                                  logos::process_return & result,
                                                  std::shared_ptr<logos::Account> info,
                                                  const Amount & token_total)
{
    // Filter out requests that don't actually
    // transfer tokens.
    if(request->GetTokenTotal() == 0)
    {
        return true;
    }

    // Lambda for validating the account
    // receiving tokens.
    auto validate_destination = [this, &result](const auto & destination_address,
                                                const auto & token_id,
                                                auto & token_account)
    {
        std::shared_ptr<logos::Account> account;

        auto found = !_store.account_get(destination_address, account);
        auto destination = dynamic_pointer_cast<logos::account_info>(account);

        if(!destination)
        {
            // The destination account type for
            // this token transfer is incorrect.
            // Only user accounts can receive
            // tokens.
            result.code = logos::process_result::invalid_request;
            return false;
        }

        TokenUserStatus destination_status;
        BlockHash token_user_id(GetTokenUserID(token_id,  destination_address));

        // We have the destination account
        if(found)
        {
            TokenEntry destination_token_entry;

            // This destination account has been tethered to
            // the token
            if(destination->GetEntry(token_id, destination_token_entry))
            {
                destination_status = destination_token_entry.status;
            }

            // This destination account is untethered
            else
            {
                // The account's token entries are
                // at maximum capacity.
                if(destination->entries.size() == logos::account_info::MAX_TOKEN_ENTRIES)
                {
                    result.code = logos::process_result::too_many_token_entries;
                    return false;
                }

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

        return true;

    }; // Lambda validate_destination

    if(request->type == RequestType::Revoke)
    {
        auto revoke = dynamic_pointer_cast<const Revoke>(request);
        auto token_account = dynamic_pointer_cast<TokenAccount>(info);

        if(!token_account || !revoke)
        {
            result.code = logos::process_result::invalid_request;
            return false;
        }

        std::shared_ptr<logos::Account> source;
        if(_store.account_get(request->GetSource(), source))
        {
            // TODO: Bootstrapping
            result.code = logos::process_result::unknown_source_account;
            return false;
        }

        if(!validate_destination(revoke->transaction.destination,
                                 revoke->token_id,
                                 token_account))
        {
            return false;
        }

        // The available tokens and the
        // amount requested don't add up.
        if(!request->Validate(result, source))
        {
            return false;
        }

        // TODO: Pending revoke cache
        //
    }

    else if(request->type == RequestType::TokenSend)
    {
        auto send_tokens = dynamic_pointer_cast<const TokenSend>(request);

        if(!send_tokens)
        {
            result.code = logos::process_result::invalid_request;
            return false;
        }

        std::shared_ptr<TokenAccount> token_account(new TokenAccount);

        // This token id doesn't exist.
        if(_store.token_account_get(send_tokens->token_id, *token_account))
        {
            result.code = logos::process_result::invalid_token_id;
            return false;
        }

        auto user_account = std::dynamic_pointer_cast<logos::account_info>(info);

        if(!user_account)
        {
            result.code = logos::process_result::invalid_request;
            return false;
        }

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
            if(!validate_destination(t.destination,
                                     send_tokens->token_id,
                                     token_account))
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

        if(!request->Validate(result, info))
        {
            return false;
        }
    }

    else if (request->type == RequestType::Distribute or
             request->type == RequestType::WithdrawFee)
    {
        auto token_account = dynamic_pointer_cast<TokenAccount>(info);
        auto token_request = dynamic_pointer_cast<const TokenRequest>(request);

        if(!token_account || !token_request)
        {
            result.code = logos::process_result::invalid_request;
            return false;
        }

        AccountAddress destination = token_request->GetDestination();

        if(destination.is_zero())
        {
            result.code = logos::process_result::invalid_request;
            return false;
        }

        if(!validate_destination(destination,
                                 token_request->token_id,
                                 token_account))
        {
            return false;
        }

        if(!request->Validate(result, info))
        {
            return false;
        }
    }

    return true;
}

void PersistenceManager<R>::StoreRequestBlock(const ApprovedRB & message,
                                              MDB_txn * transaction,
                                              uint8_t delegate_id)
{
    auto hash(message.Hash());
    LOG_DEBUG(_log) << "PersistenceManager::StoreRequestBlock - "
                    << message.Hash().to_string();

    // Check if should link with previous epoch's last batch block, starting from the second "normal" epoch (i.e. 4)
    if (!message.sequence && message.epoch_number > GENESIS_EPOCH + 1)
    {
        // should perform linking here only if after stale epoch (after an epoch block has been proposed in cur epoch)
        // if latest stored epoch number is exactly 1 behind current, then we know
        // no request block was proposed during first MB interval of cur epoch
        //   --> so epoch persistence didn't perform chain connecting --> so we have to connect here
        if (_store.epoch_number_stored() + 1 == message.epoch_number)
        {
            // Get current epoch's request block tip (updated by Epoch Persistence),
            // which is also the end of previous epoch's request block chain
            BlockHash cur_tip;
            if (_store.request_tip_get(message.primary_delegate, message.epoch_number, cur_tip))
            {
                LOG_FATAL(_log) << "PersistenceManager<BSBCT>::StoreBatchMessage failed to get request block tip for delegate "
                                << std::to_string(message.primary_delegate) << " for epoch number " << message.epoch_number;
                trace_and_halt();
            }
            // Update `next` of last request block in previous epoch
            if (_store.consensus_block_update_next(cur_tip, hash, ConsensusType::Request, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager<BSBCT>::StoreBatchMessage failed to update prev epoch's "
                                << "request block tip";
                trace_and_halt();
            }

            // Update `previous` of this block
            message.previous = cur_tip;
        }
    }

    if(_store.request_block_put(message, hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::StoreRequestBlock - "
                        << "Failed to store batch message with hash: "
                        << hash.to_string();

        trace_and_halt();
    }

    if(_store.request_tip_put(delegate_id, message.epoch_number, hash, transaction))
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
}

void PersistenceManager<R>::ApplyRequestBlock(
    const ApprovedRB & message,
    MDB_txn * transaction)
{
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        LOG_INFO(_log) << "Applying request: " 
            << message.requests[i]->Hash().to_string();
        ApplyRequest(message.requests[i],
                     message.timestamp,
                     message.epoch_number,
                     transaction);
    }
}

void PersistenceManager<R>::ApplyRequest(RequestPtr request,
                                         uint64_t timestamp,
                                         uint32_t cur_epoch_num,
                                         MDB_txn * transaction)
{

    LOG_INFO(_log) << "PersistenceManager::ApplyRequest -"
        << request->Hash().to_string();
    std::shared_ptr<logos::Account> info;
    auto account_error(_store.account_get(request->GetAccount(), info));

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
    if(request->type != RequestType::ElectionVote)
    {
        info->balance -= request->fee;
    }
    // Performs the actions required by whitelisting
    // and freezing.
    auto adjust_token_user_status = [this, &transaction](auto message, UserStatus status)
    {

        // Set the appropriate field
        // according to the required
        // status change.
        auto do_adjust_status = [status](TokenUserStatus & user_status)
        {
            switch(status)
            {
                case UserStatus::Frozen:
                    user_status.frozen = true;
                    break;
                case UserStatus::Unfrozen:
                    user_status.frozen = false;
                    break;
                case UserStatus::Whitelisted:
                    user_status.whitelisted = true;
                    break;
                case UserStatus::NotWhitelisted:
                    user_status.whitelisted = false;
                    break;
                case UserStatus::Unknown:
                    break;
            }
        };

        // Update the user's status and persist
        // the change.
        auto adjust_status = [this, &message, &transaction, status, &do_adjust_status]()
        {
            auto token_user_id = GetTokenUserID(message->token_id, message->account);

            TokenUserStatus user_status;
            _store.token_user_status_get(token_user_id, user_status, transaction);

            do_adjust_status(user_status);

            if(_store.token_user_status_put(token_user_id, user_status, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                                << "Failed to store token user status. "
                                << "Token id: "
                                << message->token_id.to_string()
                                << " User account: "
                                << message->account.to_account()
                                << " Token user id: "
                                << token_user_id.to_string();

                trace_and_halt();
            }
        };

        logos::account_info user_account;

        // Account was found
        if(!_store.account_get(message->account, user_account, transaction))
        {
            auto entry = user_account.GetEntry(message->token_id);

            // Account is tethered; use TokenEntry
            if(entry != user_account.entries.end())
            {
                do_adjust_status(entry->status);
                if(_store.account_put(message->account, user_account, transaction))
                {
                    LOG_FATAL(_log) << "PersistenceManager::ApplyRequest - "
                                    << "Failed to store account: "
                                    << message->account.to_account();
                    trace_and_halt();
                }
            }

            // Account is untethered; use the
            // central freeze/whitelist
            else
            {
                adjust_status();
            }
        }

        // Account was not found; use the
        // central freeze/whitelist
        else
        {
            adjust_status();
        }
    };

    switch(request->type)
    {
        case RequestType::Send:
        {
            auto send = dynamic_pointer_cast<const Send>(request);
            auto source = dynamic_pointer_cast<logos::account_info>(info);
            assert(send && source);

            source->balance -= send->GetLogosTotal();

            ApplySend(send,
                      timestamp,
                      transaction);
            break;
        }
        case RequestType::Change:
            break;
        case RequestType::Issuance:
        {
            auto issuance = dynamic_pointer_cast<const Issuance>(request);
            assert(issuance);

            TokenAccount account(*issuance);

            // TODO: Consider providing a TokenIsuance field
            //       for explicitly  declaring the amount of
            //       Logos designated for the account's balance.
            account.balance += request->fee - MIN_TRANSACTION_FEE;

            _store.token_account_put(issuance->token_id, account, transaction);
            break;
        }
        case RequestType::IssueAdditional:
        {
            auto issue_adtl = dynamic_pointer_cast<const IssueAdditional>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(issue_adtl && token_account);

            token_account->token_balance += issue_adtl->amount;
            token_account->total_supply += issue_adtl->amount;

            break;
        }
        case RequestType::ChangeSetting:
        {
            auto change = dynamic_pointer_cast<const ChangeSetting>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(change && token_account);

            token_account->Set(change->setting, change->value);
            break;
        }
        case RequestType::ImmuteSetting:
        {
            auto immute = dynamic_pointer_cast<const ImmuteSetting>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(immute && token_account);

            token_account->Set(TokenAccount::GetMutabilitySetting(immute->setting), false);
            break;
        }
        case RequestType::Revoke:
        {
            auto revoke = dynamic_pointer_cast<const Revoke>(request);
            assert(revoke);

            logos::account_info user_account;

            // TODO: Pending revoke cache
            if(!_store.account_get(revoke->source, user_account, transaction))
            {
                auto entry = user_account.GetEntry(revoke->token_id);
                assert(entry != user_account.entries.end());

                entry->balance -= revoke->transaction.amount;

                ReceiveBlock receive(user_account.receive_head, revoke->GetHash(), 0);
                user_account.receive_head = receive.Hash();

                PlaceReceive(receive, timestamp, transaction);

                if(_store.account_put(revoke->source, user_account, transaction))
                {
                    LOG_FATAL(_log) << "PersistenceManager::ApplyRequest - "
                                    << "Failed to store account: "
                                    << revoke->source.to_account();
                    trace_and_halt();
                }
            }

            // Couldn't find account
            else
            {
                LOG_FATAL(_log) << "PersistenceManager::ApplyRequest - "
                                << "Failed to find account: "
                                << revoke->source.to_account();
                trace_and_halt();
            }

            ApplySend(revoke->transaction,
                      timestamp,
                      transaction,
                      revoke->GetHash(),
                      revoke->token_id);

            break;
        }
        case RequestType::AdjustUserStatus:
        {
            auto adjust = dynamic_pointer_cast<const AdjustUserStatus>(request);
            assert(adjust);

            adjust_token_user_status(adjust, adjust->status);
            break;
        }
        case RequestType::AdjustFee:
        {
            auto set_fee = dynamic_pointer_cast<const AdjustFee>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(set_fee && token_account);

            token_account->fee_type = set_fee->fee_type;
            token_account->fee_rate = set_fee->fee_rate;

            break;
        }
        case RequestType::UpdateIssuerInfo:
        {
            auto issuer_info = dynamic_pointer_cast<const UpdateIssuerInfo>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(issuer_info && token_account);

            token_account->issuer_info = issuer_info->new_info;

            break;
        }
        case RequestType::UpdateController:
        {
            auto update = dynamic_pointer_cast<const UpdateController>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(update && token_account);

            auto controller = token_account->GetController(update->controller.account);

            if(update->action == ControllerAction::Add)
            {
                // Update an existing controller
                if(controller != token_account->controllers.end())
                {
                    *controller = update->controller;
                }

                // Add a new controller
                else
                {
                    token_account->controllers.push_back(update->controller);
                }
            }
            else if(update->action == ControllerAction::Remove)
            {
                assert(controller != token_account->controllers.end());
                token_account->controllers.erase(controller);
            }

            break;
        }
        case RequestType::Burn:
        {
            auto burn = dynamic_pointer_cast<const Burn>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(burn && token_account);

            token_account->total_supply -= burn->amount;
            token_account->token_balance -= burn->amount;

            break;
        }
        case RequestType::Distribute:
        {
            auto distribute = dynamic_pointer_cast<const Distribute>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(distribute && token_account);

            token_account->token_balance -= distribute->transaction.amount;

            ApplySend(distribute->transaction,
                      timestamp,
                      transaction,
                      distribute->GetHash(),
                      distribute->token_id);

            break;
        }
        case RequestType::WithdrawFee:
        {
            auto withdraw = dynamic_pointer_cast<const WithdrawFee>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(withdraw && token_account);

            token_account->token_fee_balance -= withdraw->transaction.amount;

            ApplySend(withdraw->transaction,
                      timestamp,
                      transaction,
                      withdraw->GetHash(),
                      withdraw->token_id);

            break;
        }
        case RequestType::WithdrawLogos:
        {
            auto withdraw = dynamic_pointer_cast<const WithdrawLogos>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(withdraw && token_account);

            token_account->balance -= withdraw->transaction.amount;

            ApplySend(withdraw->transaction,
                      timestamp,
                      transaction,
                      withdraw->GetHash(),
                      {0});

            break;
        }
        case RequestType::TokenSend:
        {
            auto send = dynamic_pointer_cast<const TokenSend>(request);
            auto source = dynamic_pointer_cast<logos::account_info>(info);
            assert(send && source);

            TokenAccount token_account;
            if(_store.token_account_get(send->token_id, token_account, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::ApplyRequest - "
                                << "Failed to get token account with token ID: "
                                << send->token_id.to_string();
                trace_and_halt();
            }

            token_account.token_fee_balance += send->token_fee;

            if(_store.token_account_put(send->token_id, token_account, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::ApplyRequest - "
                                << "Failed to store token account with token ID: "
                                << send->token_id.to_string();
                trace_and_halt();
            }

            auto entry = source->GetEntry(send->token_id);
            assert(entry != source->entries.end());

            entry->balance -= send->GetTokenTotal();

            ApplySend(send,
                      timestamp,
                      transaction,
                      send->token_id);

            break;
        }
        case RequestType::ElectionVote:
        {
            auto ev = dynamic_pointer_cast<const ElectionVote>(request);
            ApplyRequest(*ev,transaction);
            break;
        }
        case RequestType::AnnounceCandidacy:
        {
            auto ac = dynamic_pointer_cast<const AnnounceCandidacy>(request);
            ApplyRequest(*ac,transaction);
            break;
        }
        case RequestType::RenounceCandidacy:
        {
            auto rc = dynamic_pointer_cast<const RenounceCandidacy>(request);
            ApplyRequest(*rc,transaction);
            break;
        }
        case RequestType::StartRepresenting:
        {
            auto sr = dynamic_pointer_cast<const StartRepresenting>(request);
            ApplyRequest(*sr,transaction);
            break;
        }
        case RequestType::StopRepresenting:
        {
            auto sr = dynamic_pointer_cast<const StopRepresenting>(request);
            ApplyRequest(*sr,transaction);
            break;
        }
        case RequestType::Unknown:
            LOG_ERROR(_log) << "PersistenceManager::ApplyRequest - "
                            << "Unknown request type.";
            break;
    }

    if(_store.account_put(request->GetAccount(), info, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplyRequest - "
                        << "Failed to store account: "
                        << request->origin.to_string();

        trace_and_halt();
    }

}


template<typename SendType>
void PersistenceManager<R>::ApplySend(std::shared_ptr<const SendType> request,
                                      uint64_t timestamp,
                                      MDB_txn *transaction,
                                      BlockHash token_id)
{
    // SYL: we don't need to lock destination mutex here because updates to same account within
    // the same transaction handle will be serialized, and a lock here wouldn't do anything to
    // prevent race condition across transactions, since flushing to DB is delayed
    // (only when transaction destructor is called)
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
    std::shared_ptr<logos::Account> info;
    auto account_error(_store.account_get(send.destination, info, transaction));

    ReceiveBlock receive(
        /* Previous          */ info ? info->receive_head : 0,
        /* send_hash         */ request_hash,
        /* transaction_index */ transaction_index
    );

    auto hash(receive.Hash());

    // Destination account doesn't exist yet
    if(account_error)
    {
        info.reset(new logos::account_info);
        static_pointer_cast<logos::account_info>(info)->open_block = hash;

        LOG_DEBUG(_log) << "PersistenceManager::ApplySend - "
                        << "new account: "
                        << send.destination.to_string();
    }

    info->receive_count++;
    info->receive_head = hash;
    info->modified = logos::seconds_since_epoch();

    // This is a logos transaction
    if(token_id.is_zero())
    {
        info->balance += send.amount;
    }

    // This is a token transaction
    else
    {
        auto user_info = dynamic_pointer_cast<logos::account_info>(info);
        assert(user_info);

        auto entry = user_info->GetEntry(token_id);

        // The destination account is being
        // tethered to this token.
        if(entry == user_info->entries.end())
        {
            TokenEntry new_entry;
            new_entry.token_id = token_id;

            // TODO: Put a limit on the number
            //       of token entries a single
            //       account can have.
            user_info->entries.push_back(new_entry);

            entry = user_info->GetEntry(token_id);
            assert(entry != user_info->entries.end());

            TokenUserStatus status;
            auto token_user_id = GetTokenUserID(token_id, send.destination);

            // This user's token status has been stored
            // in the central freeze/white list.
            if(!_store.token_user_status_get(token_user_id, status, transaction))
            {
                // Once an account is tethered to a token,
                // the token entry itself will be used to
                // store the token user status.
                entry->status = status;
                if(_store.token_user_status_del(token_user_id, transaction))
                {
                    LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                                    << "Failed to delete token user status. "
                                    << "Token id: "
                                    << token_id.to_string()
                                    << " User account: "
                                    << send.destination.to_account()
                                    << " Token user id: "
                                    << token_user_id.to_string();

                    trace_and_halt();
                }
            }
        }

        entry->balance += send.amount;
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
            std::shared_ptr<Request> request;
            if(_store.request_get(b.send_hash, request, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::PlaceReceive - "
                                << "Failed to get a previous state block with hash: "
                                << b.send_hash.to_string();
                trace_and_halt();
            }

            ApprovedRB approved;
            if(_store.request_block_get(request->locator.hash, approved, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::PlaceReceive - "
                                << "Failed to get a previous batch state block with hash: "
                                << request->locator.hash.to_string();
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
            if(_store.receive_get(cur.previous,
                                   cur,
                                   transaction))
            {
                if(!cur.previous.is_zero())
                {
                    LOG_FATAL(_log) << "PersistenceManager<B>::PlaceReceive - "
                                    << "Failed to get a previous receive block with hash: "
                                    << cur.previous.to_string();
                    trace_and_halt();
                }
                break;
            }
        }

        // SYL integration fix: we only want to modify prev in DB if we are inserting somewhere in the middle of the receive chain
        if(!prev.send_hash.is_zero())
        {
            std::shared_ptr<Request> prev_request;
            if(_store.request_get(prev.send_hash, prev_request, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager<B>::PlaceReceive - "
                                << "Failed to get a previous state block with hash: "
                                << prev.send_hash.to_string();
                trace_and_halt();
            }
            if(!prev_request->origin.is_zero())
            {
                // point following receive aka prev's 'previous' field to new receive
                receive.previous = prev.previous;
                prev.previous = hash;
                auto prev_hash (prev.Hash());
                if(_store.receive_put(prev_hash, prev, transaction))
                {
                    LOG_FATAL(_log) << "PersistenceManager::PlaceReceive - "
                                    << "Failed to store receive block with hash: "
                                    << prev_hash.to_string();

                    trace_and_halt();
                }
            }
            else  // sending to burn address is already prohibited
            {
                LOG_FATAL(_log) << "PersistenceManager<B>::PlaceReceive - "
                                << "Encountered state block with empty account field, hash: "
                                << prev.send_hash.to_string();
                trace_and_halt();
            }
        }
    }
    else if (!receive.previous.is_zero())
    {
        LOG_FATAL(_log) << "PersistenceManager<B>::PlaceReceive - "
                        << "Failed to get a previous receive block with hash: "
                        << receive.previous.to_string();
        trace_and_halt();
    }

    if(_store.receive_put(hash, receive, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::PlaceReceive - "
                        << "Failed to store receive block with hash: "
                        << hash.to_string();

        trace_and_halt();
    }
}


void PersistenceManager<R>::ApplyRequest(const StartRepresenting& request, MDB_txn* txn)
{
    RepInfo rep(request);
    assert(!_store.rep_put(request.origin,rep,txn));
    assert(!_store.request_put(request,txn));
}

void PersistenceManager<R>::ApplyRequest(const StopRepresenting& request, MDB_txn* txn)
{
    RepInfo rep;
    assert(!_store.rep_get(request.origin,rep,txn));
    rep.rep_action_tip = request.Hash();
    assert(!_store.rep_put(request.origin,rep,txn));
    assert(!_store.rep_mark_remove(request.origin, txn));
    assert(!_store.request_put(request,txn));
}

void PersistenceManager<R>::ApplyRequest(const ElectionVote& request, MDB_txn* txn)
{
    RepInfo rep;
    assert(!_store.rep_get(request.origin,rep,txn));
    rep.election_vote_tip = request.Hash();
    assert(!_store.rep_put(request.origin,rep,txn));
    for(auto& p : request.votes)
    {
        assert(!_store.candidate_add_vote(
                    p.account,
                    p.num_votes*rep.stake.number(),
                    request.epoch_num,
                    txn));
    }
    assert(!_store.request_put(request,txn));
}
void PersistenceManager<R>::ApplyRequest(const AnnounceCandidacy& request, MDB_txn* txn)
{
    RepInfo rep;
    assert(!_store.rep_get(request.origin,rep, txn));
    CandidateInfo candidate(request);
    if(candidate.stake > 0)
    {
        rep.stake = candidate.stake;
    }
    else
    {
        candidate.stake = rep.stake;
    }
    rep.candidacy_action_tip = request.Hash();
    assert(!_store.rep_put(request.origin,rep,txn));
    assert(!_store.candidate_put(request.origin,candidate,txn));
    assert(!_store.request_put(request,txn));
}

void PersistenceManager<R>::ApplyRequest(const RenounceCandidacy& request, MDB_txn* txn)
{
    CandidateInfo candidate;
    if(!_store.candidate_get(request.origin, candidate, txn))
    {
        assert(!_store.candidate_mark_remove(request.origin,txn));
    }
    RepInfo rep;
    assert(!_store.rep_get(request.origin, rep, txn));
    rep.candidacy_action_tip = request.Hash();
    assert(!_store.rep_put(request.origin,rep,txn));
    assert(!_store.request_put(request,txn));
}

//TODO: dynamic can be changed to static if we do type validation
//in the constructors of ALL the request types
uint32_t GetEpochNum(std::shared_ptr<Request> req)
{
    switch(req->type)
    {
        case RequestType::AnnounceCandidacy:
        {
            auto derived = dynamic_pointer_cast<AnnounceCandidacy>(req);
            return derived->epoch_num;
        }
        case RequestType::RenounceCandidacy:
        {
            auto derived = dynamic_pointer_cast<RenounceCandidacy>(req);
            return derived->epoch_num;
        }
        case RequestType::StartRepresenting:
        {
            auto derived = dynamic_pointer_cast<StartRepresenting>(req);
            return derived->epoch_num;
        }
        case RequestType::StopRepresenting:
        {
            auto derived = dynamic_pointer_cast<StopRepresenting>(req);
            return derived->epoch_num;
        }
        case RequestType::ElectionVote:
        {
            auto derived = dynamic_pointer_cast<ElectionVote>(req);
            return derived->epoch_num;
        }
        default:
            trace_and_halt();
    }
}

bool PersistenceManager<R>::ValidateRequest(const ElectionVote& vote_request, uint32_t cur_epoch_num, MDB_txn* txn, logos::process_return & result)
{
    if(vote_request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }
    if(cur_epoch_num < EpochVotingManager::START_ELECTIONS_EPOCH)
    {
        result.code = logos::process_result::no_elections;
        return false;
    }

    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }
    RepInfo rep;
    //are you a rep at all?
    if(_store.rep_get(vote_request.origin,rep,txn))
    {
        result.code = logos::process_result::not_a_rep;
        return false;
    }

    //What is your status as a rep
    auto hash = rep.rep_action_tip;
    assert(hash != 0);
    std::shared_ptr<Request> rep_req;
    assert(!_store.request_get(hash, rep_req, txn));
    assert(rep_req->type == RequestType::StartRepresenting
            || rep_req->type == RequestType::StopRepresenting);
    uint32_t rep_req_epoch = GetEpochNum(rep_req);
    if(rep_req->type == RequestType::StartRepresenting 
            && rep_req_epoch == cur_epoch_num) 
    {
        result.code = logos::process_result::pending_rep_action;
        return false;
    }
    else if(rep_req->type == RequestType::StopRepresenting
            && rep_req_epoch < cur_epoch_num)
    {
        result.code = logos::process_result::not_a_rep;
        return false;
    }

    //did you vote already this epoch?
    hash = rep.election_vote_tip;
    if(hash != 0)
    {
        std::shared_ptr<Request> vote_req;
        assert(!_store.request_get(hash, vote_req, txn));
        if(GetEpochNum(vote_req) == cur_epoch_num)
        {
            result.code = logos::process_result::already_voted;
            return false;
        }
    }

    size_t total = 0;
    //are these proper votes?
    for(auto& cp : vote_request.votes)
    {
        total += cp.num_votes;
        CandidateInfo info;
        //check account is in candidacy_db
        if(_store.candidate_get(cp.account,info,txn))
        {
            result.code = logos::process_result::invalid_candidate;
            return false;
        }
        else//check account is active candidate
        {
            RepInfo c_rep;
            assert(!_store.rep_get(cp.account, c_rep, txn));
            auto hash = c_rep.candidacy_action_tip;
            assert(hash != 0);
            std::shared_ptr<Request> candidacy_req;
            assert(!_store.request_get(hash, candidacy_req, txn));
            if(candidacy_req->type == RequestType::AnnounceCandidacy
                    && GetEpochNum(candidacy_req) == cur_epoch_num)
            {
                result.code = logos::process_result::invalid_candidate;
                return false;
            }
           /*
            * If candidate renounced, the renounce was this epoch, which means
            * they can still receive votes this epoch. If renounce was in
            * previous epoch, should have been removed at epoch transition and
            * first if condition would have been met
            */ 
        }
    }
    return total <= MAX_VOTES;
}

bool PersistenceManager<R>::ValidateRequest(
        const AnnounceCandidacy& request,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }
    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }
    if(cur_epoch_num < EpochVotingManager::START_ELECTIONS_EPOCH - 1)
    {
        result.code = logos::process_result::no_elections;
        return false;
    }
    RepInfo rep;
    if(_store.rep_get(request.origin,rep,txn))
    {
        result.code = logos::process_result::not_a_rep;
        return false;
    }
    Amount stake = request.stake != 0 ? request.stake : rep.stake;
    if(stake < MIN_DELEGATE_STAKE)
    {
        result.code = logos::process_result::not_enough_stake;
        return false;
    }

    //what is your status as a rep?
    std::shared_ptr<Request> rep_request;
    auto hash = rep.rep_action_tip;
    assert(hash != 0);
    assert(!_store.request_get(hash, rep_request, txn));
    assert(rep_request->type == RequestType::StartRepresenting
            || rep_request->type == RequestType::StopRepresenting);
    if(GetEpochNum(rep_request) == cur_epoch_num)
    {
        result.code = logos::process_result::pending_rep_action;
        return false;
    }


    //what is your status as a candidate?
    std::shared_ptr<Request> candidacy_req;
    hash = rep.candidacy_action_tip;
    if(hash != 0)
    {
        assert(!_store.request_get(hash,candidacy_req,txn));
        auto type = candidacy_req->type;
        assert(type == RequestType::AnnounceCandidacy || type == RequestType::RenounceCandidacy);
        if(type == RequestType::AnnounceCandidacy)
        {
            result.code = logos::process_result::already_announced_candidacy;
            return false;
        }
        else if(GetEpochNum(candidacy_req) == cur_epoch_num) //RenounceCandidacy
        {
            result.code = logos::process_result::pending_candidacy_action;
            return false;
        }
    }
    return true;
}

bool PersistenceManager<R>::ValidateRequest(
        const RenounceCandidacy& request,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }

    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }
    RepInfo rep;
    if(_store.rep_get(request.origin,rep,txn))
    {
        result.code = logos::process_result::not_a_rep;
        return false;
    }
    //what is your candidacy status?
    auto hash = rep.candidacy_action_tip;
    if(hash == 0)
    {
        result.code = logos::process_result::never_announced_candidacy;
        return false;
    }
    shared_ptr<Request> candidacy_req;
    assert(!_store.request_get(hash, candidacy_req, txn));
    assert(candidacy_req->type == RequestType::AnnounceCandidacy
            || candidacy_req->type == RequestType::RenounceCandidacy);
    if(candidacy_req->type == RequestType::RenounceCandidacy)
    {
        result.code = logos::process_result::already_renounced_candidacy;
        return false;
    }
    else if(GetEpochNum(candidacy_req) == cur_epoch_num) //AnnounceCandidacy
    {
        result.code = logos::process_result::pending_candidacy_action;
        return false;
    }
    return true;
}

/*
 * The dead period is the time between when the epoch starts and when the epoch
 * block is created. The reason for disallowing votes during this time is because
 * the delegates do not come to consensus on the election results until the epoch
 * block is created. If someone attempts to vote for a candidate during the dead
 * period who was also a candidate in the last epoch, a delegate cannot reliably
 * say whether the vote is valid: if that candidate won the election, the vote is
 * invalid but if the candidate did not win, the vote is valid
 */
bool PersistenceManager<R>::IsDeadPeriod(uint32_t cur_epoch_num, MDB_txn* txn)
{
    BlockHash hash; 
    assert(!_store.epoch_tip_get(hash,txn));

    ApprovedEB eb;
    assert(!_store.epoch_get(hash,eb,txn));

    return (eb.epoch_number+2) == cur_epoch_num;
}

bool PersistenceManager<R>::ValidateRequest(
        const StartRepresenting& request,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }
    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }

    if(request.stake < MIN_REP_STAKE)
    {
        result.code = logos::process_result::not_enough_stake;
        return false;
    } 

    RepInfo rep;
    if(!_store.rep_get(request.origin,rep,txn))
    {
        auto hash = rep.rep_action_tip;
        assert(hash != 0);
        std::shared_ptr<Request> rep_req;
        assert(!_store.request_get(hash, rep_req, txn));
        assert(rep_req->type == RequestType::StartRepresenting
                || rep_req->type == RequestType::StopRepresenting);
        if(rep_req->type == RequestType::StartRepresenting)
        {

            result.code = logos::process_result::is_rep;
            return false;
        }
        else if(GetEpochNum(rep_req) == cur_epoch_num) //StopRepresenting
        {
            result.code = logos::process_result::pending_rep_action;
            return false;
        }
    }
    return true;
}

bool PersistenceManager<R>::ValidateRequest(
        const StopRepresenting& request,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }

    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }

    CandidateInfo candidate;
    if(!_store.candidate_get(request.origin,candidate,txn))
    {
        //cant stop representing if you are a candidate
        result.code = logos::process_result::is_candidate;
        return false;
    }

    ApprovedEB epoch;
    BlockHash hash;
    assert(!_store.epoch_tip_get(hash,txn));
    assert(!_store.epoch_get(hash,epoch,txn));

    for(size_t i = 0; i < NUM_DELEGATES; ++i)
    {
        if(epoch.delegates[i].account == request.origin)
        {
            //can't stop representing if you are a delegate or delegate elect
            result.code = logos::process_result::is_delegate;
            return false;
        }
    }

    RepInfo rep;
    if(!_store.rep_get(request.origin,rep,txn))
    {
        auto hash = rep.rep_action_tip;
        assert(hash != 0);
        std::shared_ptr<Request> rep_request;
        assert(!_store.request_get(hash, rep_request, txn));
        assert(rep_request->type == RequestType::StartRepresenting
                || rep_request->type == RequestType::StopRepresenting);

        if(GetEpochNum(rep_request) == cur_epoch_num)
        {
            result.code = logos::process_result::pending_rep_action;
            return false;
        }
        else if(rep_request->type == RequestType::StopRepresenting) //stopped in previous epoch
        {
            result.code = logos::process_result::not_a_rep;
            return false;
        }
        return true;
    }
    result.code = logos::process_result::not_a_rep;
    return false;
}


