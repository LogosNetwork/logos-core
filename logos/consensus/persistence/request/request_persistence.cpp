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
#include <logos/rewards/epoch_rewards_manager.hpp>
#include <logos/staking/voting_power_manager.hpp>
#include <logos/staking/staking_manager.hpp>
#include <logos/node/node.hpp>

std::mutex PersistenceManager<R>::_write_mutex;

// TODO: Dynamic can be changed to static if we do type validation
//       in the constructors of ALL the request types.
//
uint32_t GetEpochNum(std::shared_ptr<Request> req)
{
    auto governance_request = dynamic_pointer_cast<Governance>(req);

    if(!governance_request)
    {
        trace_and_halt();
    }

    return governance_request->epoch_num;
}

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

    std::lock_guard<std::mutex> lock (_write_mutex);
    if (BlockExists(message))
    {
        LOG_DEBUG(_log) << "PersistenceManager<R>::ApplyUpdates - request block already exists, ignoring";
        // Integration Fix: Need to still unreserve. A delegate could receive
        // a post committed block via p2p, and then later receive a preprepare
        // for the same block (particularly if consensus is in p2p mode). 
        // Delegate would reserve when receiving the preprepare, but on
        // post-commit, the block already exists. Need to release reservation
        for(uint16_t i = 0; i < message.requests.size(); ++i)
        {
            Release(message.requests[i]);
        }

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

    // Need to ensure the operations below execute atomically
    // Otherwise, multiple calls to batch persistence may overwrite balance for the same account
    {
        //Note, creating a write transaction blocks if another write transaction
        //exists elsewhere
        logos::transaction transaction(_store.environment, nullptr, true);
        StoreRequestBlock(message, transaction, delegate_id);
        ApplyRequestBlock(message, transaction);
    }

    // SYL Integration: clear reservation AFTER flushing to LMDB to ensure safety
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        auto request = message.requests[i];
        Release(request);
    }
}

void PersistenceManager<R>::Release(RequestPtr request)
{
    _reservations->Release(request->GetAccount(),request->digest);

    if(request->type == RequestType::Revoke || request->type == RequestType::TokenSend)
    {
        auto token_request = dynamic_pointer_cast<const TokenRequest>(request);
        assert(token_request);

        auto token_user_id = GetTokenUserID(token_request->token_id,
                                            token_request->GetSource());

        // Also release the TokenUserID reservation.
        _reservations->Release(token_user_id,request->digest);
    }
}

bool PersistenceManager<R>::BlockExists(
    const ApprovedRB & message)
{
    return _store.request_block_exists(message);
}

template <typename T>
bool ValidateGovernanceSubchain(
    T const & req,
    logos::account_info const & info,
    logos::process_return & result,
    MDB_txn *txn)
{
    if(info.governance_subchain_head != req.governance_subchain_prev)
    {
        result.code = logos::process_result::invalid_governance_subchain;
        return false;
    }
    return true;
}

template <typename T>
bool ValidateStake(
        T const & req,
        logos::account_info const & info,
        logos::process_return& result,
        MDB_txn* txn)
{
    if(req.set_stake)
    {
        bool can_stake = StakingManager::GetInstance()->Validate(
                req.origin,
                info,
                req.stake,
                req.origin,
                req.epoch_num,
                req.fee,
                txn);
        if(!can_stake)
        {
            result.code = logos::process_result::insufficient_funds_for_stake;
            return false;
        }
    }

    return true;
}


bool PersistenceManager<R>::ValidateRequest(
    RequestPtr request,
    uint32_t cur_epoch_num,
    logos::process_return & result,
    bool allow_duplicates,
    bool prelim)
{
    auto hash = request->GetHash();
    LOG_INFO(_log) << "PersistenceManager::ValidateRequest - validating request " << hash.to_string();

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

    // A conflicting reservation exists
    if (!_reservations->CanAcquire(request->GetAccount(), hash, allow_duplicates))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate - Account is already reserved. "
                        << "Account: " << request->GetAccount().to_account();

        result.code = logos::process_result::already_reserved;
        return false;
    }

    // Revoke and TokenSend messages require an additional reservation
    // for the 'source'. To avoid reservation conflicts with irrelevant
    // requests from this user's account, tie this secondary reservation
    // to the user's token user id, rather than their account address.
    if(request->type == RequestType::Revoke || request->type == RequestType::TokenSend)
    {
        auto token_request = dynamic_pointer_cast<const TokenRequest>(request);

        if(!token_request)
        {
            result.code = logos::process_result::invalid_request;
            return false;
        }

        auto token_user_id = GetTokenUserID(token_request->token_id,
                                            token_request->GetSource());

        // A conflicting reservation exists
        if (!_reservations->CanAcquire(token_user_id,
                                       hash,
                                       allow_duplicates))
        {
            LOG_ERROR(_log) << "PersistenceManager::Validate - Token User ID is already reserved. "
                            << "Token User ID: "
                            << token_user_id.to_string();

            result.code = logos::process_result::already_reserved;
            return false;
        }
    }

    // Set prelim to true single transaction (non-batch) validation from TxAcceptor, false for RPC
    if (prelim)
    {
        result.code = logos::process_result::progress;
        return true;
    }

    // Move on to check account info

    // No previous block set.
    //    if(request->previous.is_zero() && info->block_count)
    //    {
    //        result.code = logos::process_result::fork;
    //        return false;
    //    }

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
    else
    {
        if(!request->previous.is_zero())
        {
            result.code = logos::process_result::gap_previous;
            LOG_WARN (_log) << "GAP_PREVIOUS: account has no previous requests, current request sqn="
                            << request->sequence;
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
        // Also cover the case: (request->previous.is_zero() && info->block_count), which can be an old block or a fork
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
    //sequence number
    else if(info->block_count != request->sequence)
    {
        result.code = logos::process_result::wrong_sequence_number;
        LOG_INFO(_log) << "wrong_sequence_number, request sqn=" << request->sequence
                       << " expecting=" << info->block_count;
        return false;
    }
    else
    {
        LOG_TRACE(_log) << "right_sequence_number, request sqn=" << request->sequence
                       << " expecting=" << info->block_count;
    }

    // Make sure there's enough Logos
    // to cover the request.
    if(request->GetLogosTotal() > info->GetAvailableBalance())
    {
        //For a LogosAccount (as opposed to a TokenAccount), software needs to
        //check if there are any thawing funds that have expired, therefore
        //increasing the available balance. Thawing funds are not spendable, but
        //have an expiration epoch, during and after which the funds become spendable
        //and are no longer thawing
        if(info->type == logos::AccountType::LogosAccount)
        {
            logos::transaction txn(_store.environment, nullptr, false);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            auto sm = StakingManager::GetInstance();
            //Note, this call does not actually prune the thawing funds (and subsequently
            //update available_balance), but simply calculates the additional funds
            //that would be available if pruning were to occur. Pruning is always
            //done when the request is applied, as opposed to validated
            Amount pruneable = sm->GetPruneableThawingAmount(
                    request->origin,
                    *account_info,
                    cur_epoch_num,
                    txn);
            if(request->GetLogosTotal() > info->GetAvailableBalance() + pruneable)
            {
                result.code = logos::process_result::insufficient_balance;
                return false;
            }
        }
        else
        {
            result.code = logos::process_result::insufficient_balance;
            return false;
        }
    }

    switch(request->type)
    {
        case RequestType::Send:
            break;
        case RequestType::Proxy:
        {
            if(!ValidateRequestWithStaking<Proxy>(request,info,cur_epoch_num,result))
            {
                return false;
            }
            break;
        }
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
            if(!ValidateRequestWithStaking<ElectionVote>(request,info,cur_epoch_num,result))
            {
                return false;
            }

            break;
        }
        case RequestType::AnnounceCandidacy:
        {
            if(!ValidateRequestWithStaking<AnnounceCandidacy>(request,info,cur_epoch_num,result))
            {
                return false;
            }

            break;
        }
        case RequestType::RenounceCandidacy:
        {
            if(!ValidateRequestWithStaking<RenounceCandidacy>(request,info,cur_epoch_num,result))
            {
                return false;
            }
            break;
        }
        case RequestType::StartRepresenting:
        {
            if(!ValidateRequestWithStaking<StartRepresenting>(request,info,cur_epoch_num,result))
            {
                return false;
            }
            break;
        }
        case RequestType::StopRepresenting:
        {
            if(!ValidateRequestWithStaking<StopRepresenting>(request,info,cur_epoch_num,result))
            {
                return false;
            }
            break;
        }
        case RequestType::Stake:
        {

            if(!ValidateRequestWithStaking<Stake>(request,info,cur_epoch_num,result))
            {
                return false;
            }
            break;
        }
        case RequestType::Unstake:
        {
 
            if(!ValidateRequestWithStaking<Unstake>(request,info,cur_epoch_num,result))
            {
                return false;
            }
            break;
        }
        case RequestType::Claim:
            if(!ValidateRequest(*dynamic_pointer_cast<const Claim>(request),
                                result,
                                info))
            {
                return false;
            }

            break;
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
                   << "request is : " << request->Hash().to_string()
                   <<  " . result is "
                   << success;

    if (success)
    {
        _reservations->UpdateReservation(request->GetHash(), request->GetAccount());

        if(request->type == RequestType::Revoke || request->type == RequestType::TokenSend)
        {
            auto token_request = dynamic_pointer_cast<const TokenRequest>(request);
            assert(token_request);

            auto token_user_id = GetTokenUserID(token_request->token_id,
                                                token_request->GetSource());

            // Also update the TokenUserID reservation.
            _reservations->UpdateReservation(request->GetHash(), token_user_id);
        }
    }

    return success;
}

bool PersistenceManager<R>::ValidateBatch(
    const PrePrepare & message, RejectionMap & rejection_map)
{
    // SYL Integration: use _write_mutex because we have to wait for other database writes to finish flushing
    bool valid = true;
    bool need_bootstrap = false;
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
            if(!need_bootstrap)
            {
            	if(logos::MissingBlock(ignored_result.code))
            	{
            		need_bootstrap = true;
            	}
            }
        }
    }
    if(need_bootstrap)
    {
    	// TODO: high speed Bootstrapping
    	LOG_DEBUG(_log) << "PersistenceManager<R>::ValidateBatch"
                        << " Try Bootstrap...";
        logos_global::Bootstrap();
    }
    return valid;
}

//Do not bootstrap here, since this function is called by NonDelPersistenceManager which is used by P2P and bootstrap
bool PersistenceManager<R>::Validate(const PrePrepare & message,
                                     ValidationStatus * status)
{
    using namespace logos;

    if (!status || status->progress < RVP_REQUESTS_DONE)
    {
        bool valid = true;
        std::lock_guard<std::mutex> lock (_write_mutex);

        for(uint16_t i = 0; i < message.requests.size(); ++i)
        {
            if (!status || status->progress < RVP_REQUESTS_FIRST || status->requests.find(i) != status->requests.end())
            {
                logos::process_return   result;
                LOG_INFO(_log) << "PersistenceManager::Validate - "
                    << "attempting to validate : " << message.requests[i]->Hash().to_string();
                if (!ValidateRequest(message.requests[i], message.epoch_number, result, true, false))
                {
                    UpdateStatusRequests(status, i, result.code);
                    UpdateStatusReason(status, process_result::invalid_request);
                    LOG_INFO(_log) << "PersistenceManager::Validate - "
                        << "failed to validate request : " << message.requests[i]->Hash().to_string()
                                << " error_code=" << ProcessResultToString(result.code);

                    valid = false;
                }
                else if (status && status->progress >= RVP_REQUESTS_FIRST)
                {
                    status->requests.erase(i);
                }
            }
        }

        if (status)
            status->progress = RVP_REQUESTS_FIRST;

        if (!valid)
            return false;

        if (status)
            status->progress = RVP_REQUESTS_DONE;
    }

    return true;
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
            result.code = logos::process_result::unknown_source_account;
            // TODO: high speed Bootstrapping
            logos_global::Bootstrap();
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
            Tip cur_tip;
            BlockHash & cur_tip_hash = cur_tip.digest;
            if (_store.request_tip_get(message.primary_delegate, message.epoch_number, cur_tip))
            {
                LOG_FATAL(_log) << "PersistenceManager<BSBCT>::StoreBatchMessage failed to get request block tip for delegate "
                                << std::to_string(message.primary_delegate) << " for epoch number " << message.epoch_number;
                trace_and_halt();
            }
            // Update `next` of last request block in previous epoch
            if (_store.consensus_block_update_next(cur_tip_hash, hash, ConsensusType::Request, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager<BSBCT>::StoreBatchMessage failed to update prev epoch's "
                                << "request block tip";
                trace_and_halt();
            }

            // Update `previous` of this block
            message.previous = cur_tip_hash;
        }
    }

    if(_store.request_block_put(message, hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::StoreRequestBlock - "
                        << "Failed to store batch message with hash: "
                        << hash.to_string();

        trace_and_halt();
    }

    if(_store.request_tip_put(delegate_id, message.epoch_number, message.CreateTip(), transaction))
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
            LOG_FATAL(_log) << "PersistenceManager::StoreRequestBlock - Failed to update prev block's next field";
            trace_and_halt();
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

    auto hash = request->GetHash();

    // This can happen when a duplicate request
    // is accepted. We can ignore this transaction.
    if(request->previous != info->head)
    {
        if (hash == info->head || _store.request_exists(hash))
        {
            LOG_INFO(_log) << "PersistenceManager<R>::ApplyRequest - Block previous ("
                           << request->previous.to_string()
                           << ") does not match account head ("
                           << info->head.to_string()
                           << "). Suspected duplicate request - "
                           << "ignoring.";
            return;
        }

        // Somehow a fork slipped through
        else
        {
            LOG_FATAL(_log) << "PersistenceManager<R>::ApplyRequest - encountered fork with hash "
                            << hash.to_string();
            trace_and_halt();
        }
    }

    info->block_count++;
    info->head = request->GetHash();
    info->modified = logos::seconds_since_epoch();
    if(info->type == logos::AccountType::LogosAccount)
    {
        auto account_info = dynamic_pointer_cast<logos::account_info>(info);
        StakingManager::GetInstance()->PruneThawing(request->origin, *account_info, cur_epoch_num, transaction);
    }

    if(request->type != RequestType::ElectionVote)
    {
        info->SetBalance(info->GetBalance() - request->fee, cur_epoch_num, transaction);

        auto fee = request->type != RequestType::Issuance ?
                   request->fee :
                   MinTransactionFee(request->type);

        EpochRewardsManager::GetInstance()->OnFeeCollected(cur_epoch_num, fee, transaction);
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

            //already harvested fee
            source->SetBalance(
                    source->GetBalance() - send->GetLogosTotal() + request->fee,
                    cur_epoch_num,
                    transaction);

            ApplySend(send,
                      timestamp,
                      transaction,
                      cur_epoch_num);
            break;
        }
        case RequestType::Proxy:
        {
            auto proxy = dynamic_pointer_cast<const Proxy>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            ApplyRequest(*proxy,*account_info,transaction);
            break;
        }
        case RequestType::Issuance:
        {
            auto issuance = dynamic_pointer_cast<const Issuance>(request);
            assert(issuance);

            TokenAccount account(*issuance);

            // TODO: Consider providing a TokenIsuance field
            //       for explicitly  declaring the amount of
            //       Logos designated for the account's balance.
            account.SetBalance(account.GetBalance() + request->fee - MinTransactionFee(request->type),
                               cur_epoch_num,
                               transaction);

            // SG: put Issuance Request on TokenAccount's receive chain as genesis receive,
            // update TokenAccount's relevant fields
            ReceiveBlock receive(0, issuance->GetHash(), 0);
            account.receive_head = receive.Hash();
            account.receive_count++;
            account.modified = logos::seconds_since_epoch();
            account.issuance_request = receive.Hash();

            _store.token_account_put(issuance->token_id, account, transaction);

            PlaceReceive(receive, timestamp, transaction);

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

                ReceiveBlock receive(user_account.receive_head, revoke->GetHash(), Revoke::REVOKE_OFFSET);
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
                      revoke->token_id,
                      revoke->origin,
                      cur_epoch_num);

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
                // SG: Add individual controller privileges to existing controller
                if(controller != token_account->controllers.end())
                {
                    for(int i=0; i<CONTROLLER_PRIVILEGE_COUNT; i++)
                    {
                        if (update->controller.privileges[i])
                        {
                            controller->privileges.Set(i, true);
                        }
                    }
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
                // SG: Remove individual privileges from existing controller
                bool remove_all = true;
                for(int i=0; i<CONTROLLER_PRIVILEGE_COUNT; i++)
                {
                    if (update->controller.privileges[i])
                    {
                        controller->privileges.Set(i, false);
                        remove_all = false;
                    }

                }

                // SG: Remove entire controller if no privileges specified
                if(remove_all)
                {
                    token_account->controllers.erase(controller);
                }
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
                      distribute->token_id,
                      distribute->origin,
                      cur_epoch_num);

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
                      withdraw->token_id,
                      withdraw->origin,
                      cur_epoch_num);

            break;
        }
        case RequestType::WithdrawLogos:
        {
            auto withdraw = dynamic_pointer_cast<const WithdrawLogos>(request);
            auto token_account = dynamic_pointer_cast<TokenAccount>(info);
            assert(withdraw && token_account);

            token_account->SetBalance(token_account->GetBalance() - withdraw->transaction.amount, cur_epoch_num, transaction);

            ApplySend(withdraw->transaction,
                      timestamp,
                      transaction,
                      withdraw->GetHash(),
                      {0},
                      withdraw->origin,
                      cur_epoch_num);

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
                      cur_epoch_num,
                      send->token_id);

            break;
        }
        case RequestType::ElectionVote:
        {
            auto ev = dynamic_pointer_cast<const ElectionVote>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            ApplyRequest(*ev,*account_info,transaction);
            break;
        }
        case RequestType::AnnounceCandidacy:
        {
            auto ac = dynamic_pointer_cast<const AnnounceCandidacy>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            ApplyRequest(*ac,*account_info,transaction);
            break;
        }
        case RequestType::RenounceCandidacy:
        {
            auto rc = dynamic_pointer_cast<const RenounceCandidacy>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            ApplyRequest(*rc,*account_info,transaction);
            break;
        }
        case RequestType::StartRepresenting:
        {
            auto sr = dynamic_pointer_cast<const StartRepresenting>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            ApplyRequest(*sr,*account_info,transaction);
            break;
        }
        case RequestType::StopRepresenting:
        {
            auto sr = dynamic_pointer_cast<const StopRepresenting>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            ApplyRequest(*sr,*account_info,transaction);
            break;
        }
        case RequestType::Stake:
        {
            auto stake = dynamic_pointer_cast<const Stake>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            ApplyRequest(*stake,*account_info,transaction);
            break;
        }
        case RequestType::Unstake:
        {
            auto unstake = dynamic_pointer_cast<const Unstake>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);
            ApplyRequest(*unstake,*account_info,transaction);
            break;
        }
        case RequestType::Claim:
        {
            auto claim = dynamic_pointer_cast<const Claim>(request);
            auto account_info = dynamic_pointer_cast<logos::account_info>(info);

            auto sum = ProcessClaim(claim, *account_info, transaction);
            auto deposit_amount = std::get<0>(sum);

            account_info->dust += std::get<1>(sum);

            if(account_info->dust.numerator() >= account_info->dust.denominator())
            {
                ++deposit_amount;
                --account_info->dust;
            }

            if(deposit_amount > 0)
            {
                Transaction<Amount> t(claim->origin,
                                      deposit_amount);

                ApplySend(t,
                          timestamp,
                          transaction,
                          request->GetHash(),
                          {0},
                          request->origin,
                          cur_epoch_num,
                          info);
            }

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

uint128_t PersistenceManager<R>::MinTransactionFee(RequestType type)
{
    uint128_t fee = 0;

    // TODO: Decide on distinct values for each
    //       request type.
    switch(type)
    {
        case RequestType::Send:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Proxy:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Issuance:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::IssueAdditional:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::ChangeSetting:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::ImmuteSetting:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Revoke:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::AdjustUserStatus:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::AdjustFee:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::UpdateIssuerInfo:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::UpdateController:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Burn:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Distribute:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::WithdrawFee:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::WithdrawLogos:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::TokenSend:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::ElectionVote:
            break;
        case RequestType::AnnounceCandidacy:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::RenounceCandidacy:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::StartRepresenting:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::StopRepresenting:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Stake:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Unstake:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Claim:
            fee = 0x21e19e0c9bab2400000_cppui128;
            break;
        case RequestType::Unknown:
            break;
    }

    return fee;
}

template<typename SendType>
void PersistenceManager<R>::ApplySend(std::shared_ptr<const SendType> request,
                                      uint64_t timestamp,
                                      MDB_txn *transaction,
                                      uint32_t const & epoch_num,
                                      BlockHash token_id)
{
    // SYL: we don't need to lock destination mutex here because updates to same account within
    // the same transaction handle will be serialized, and a lock here wouldn't do anything to
    // prevent race condition across transactions, since flushing to DB is delayed
    // (only when transaction destructor is called)
    uint16_t transaction_index = 0;
    for(auto & t : request->transactions)
    {
        if(request->type == RequestType::Send
                && t.destination == request->origin)
        {
            continue;
        }
        ApplySend(t,
                  timestamp,
                  transaction,
                  request->GetHash(),
                  token_id,
                  request->origin,
                  epoch_num,
                  transaction_index++);
    }
}

template<typename AmountType>
void PersistenceManager<R>::ApplySend(const Transaction<AmountType> &send,
                                      uint64_t timestamp,
                                      MDB_txn *transaction,
                                      const BlockHash &request_hash,
                                      const BlockHash &token_id,
                                      const AccountAddress& origin,
                                      uint32_t const & epoch_num,
                                      std::shared_ptr<logos::Account> &info,
                                      uint16_t transaction_index)
{
    ReceiveBlock receive(
        /* Previous          */ info ? info->receive_head : 0,
        /* source_hash       */ request_hash,
        /* transaction_index */ transaction_index
    );

    auto hash(receive.Hash());

    // Destination account doesn't exist yet
    if(!info)
    {
        info.reset(new logos::account_info);
        static_pointer_cast<logos::account_info>(info)->open_block = hash;

        std::shared_ptr<logos::Account> origin_account_info;
        if(_store.account_get(origin, origin_account_info, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                            << "failed to get origin account";
            trace_and_halt();
        }

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
        info->SetBalance(info->GetBalance() + send.amount, epoch_num, transaction);
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

    PlaceReceive(receive, timestamp, transaction);
}

template<typename AmountType>
void PersistenceManager<R>::ApplySend(const Transaction<AmountType> &send,
                                      uint64_t timestamp,
                                      MDB_txn *transaction,
                                      const BlockHash &request_hash,
                                      const BlockHash &token_id,
                                      const AccountAddress& origin,
                                      uint32_t const & epoch_num,
                                      uint16_t transaction_index)
{
    std::shared_ptr<logos::Account> info;

    if(_store.account_get(send.destination, info, transaction))
    {
        info.reset();
    }

    ApplySend(send,
              timestamp,
              transaction,
              request_hash,
              token_id,
              origin,
              epoch_num,
              info,
              transaction_index);

    if(_store.account_put(send.destination, info, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplySend - "
                        << "Failed to store account: "
                        << send.destination.to_string();

        std::exit(EXIT_FAILURE);
    }
}

void PersistenceManager<R>::ApplyRequest(
        const StartRepresenting& request,
        logos::account_info& info,
        MDB_txn* txn)
{
    assert(txn != nullptr);
    info.governance_subchain_head = request.GetHash();
    RepInfo rep(request);
    assert(!_store.rep_put(request.origin,rep,txn));
    assert(!_store.request_put(request,txn));

    if(request.set_stake)
    {
        StakingManager::GetInstance()->Stake(
                request.origin,
                info,
                request.stake,
                request.origin,
                request.epoch_num,
                txn); 
    }
}

void PersistenceManager<R>::ApplyRequest(
        const StopRepresenting& request,
        logos::account_info& info,
        MDB_txn* txn)
{
    assert(txn != nullptr);
    info.governance_subchain_head = request.GetHash();
    RepInfo rep;
    assert(!_store.rep_get(request.origin,rep,txn));
    rep.rep_action_tip = request.GetHash();

    assert(!_store.rep_put(request.origin,rep,txn));
    assert(!_store.rep_mark_remove(request.origin, txn));
    assert(!_store.request_put(request,txn));
    if(request.set_stake)
    {
        StakingManager::GetInstance()->Stake(
                request.origin,
                info,
                request.stake,
                request.origin,
                request.epoch_num,
                txn); 
    }
}

void PersistenceManager<R>::ApplyRequest(
        const ElectionVote& request,
        logos::account_info& info,
        MDB_txn* txn)
{
    assert(txn != nullptr);
    info.governance_subchain_head = request.GetHash();
    assert(!_store.request_put(request,txn));
    RepInfo rep;
    assert(!_store.rep_get(request.origin,rep,txn));
    rep.election_vote_tip = request.GetHash();
    assert(!_store.rep_put(request.origin,rep,txn));
    Amount voting_power = VotingPowerManager::GetInstance()->GetCurrentVotingPower(request.origin, request.epoch_num, txn);
    for(auto& p : request.votes)
    {
        assert(!_store.candidate_add_vote(
                    p.account,
                    p.num_votes*voting_power.number(),
                    request.epoch_num,
                    txn));
    }

    VotingPowerInfo vpi;
    auto result = VotingPowerManager::GetInstance()->GetVotingPowerInfo(request.origin, request.epoch_num, vpi, txn);
    assert(result);

    RepEpochInfo rewards_info{rep.levy_percentage,
                              request.epoch_num,
                              vpi.current.self_stake + vpi.current.locked_proxied,
                              vpi.current.self_stake};

    EpochRewardsManager::GetInstance()->Init(request.origin, rewards_info, txn); 
}

void PersistenceManager<R>::ApplyRequest(
        const AnnounceCandidacy& request,
        logos::account_info& info,
        MDB_txn* txn)
{
    assert(txn != nullptr);
    info.governance_subchain_head = request.GetHash();
    RepInfo rep;
    if(_store.rep_get(request.origin,rep, txn))
    {
        //if not a rep already, make rep
        rep = RepInfo(request);
    }
    CandidateInfo candidate(request);
    if(!request.set_stake)
    {
        StakedFunds cur_stake;
        bool has_stake = StakingManager::GetInstance()->GetCurrentStakedFunds(
                request.origin,
                cur_stake,
                txn);
        if(!has_stake)
        {
            LOG_FATAL(_log) << "PersistenceManager<R>::ApplyRequest (AnnounceCandidacy) - "
                << " cur stake is empty";
            trace_and_halt();
        }
        candidate.next_stake = cur_stake.amount;
    }
    rep.candidacy_action_tip = request.Hash();
    assert(!_store.rep_put(request.origin,rep,txn));
    ApprovedEB eb;
    assert(!_store.epoch_get_n(0, eb, txn));
    //if account is current delegate, only add to candidates if in last epoch of term
    //otherwise, epoch persistence mgr will add at proper time
    bool add_to_candidates_db = true;
    for(size_t i = 0; i < NUM_DELEGATES; ++i)
    {
        if(eb.delegates[i].account == request.origin)
        {
            add_to_candidates_db = false;
            assert(!_store.epoch_get_n(3, eb, txn));
            for(size_t j = 0; i < NUM_DELEGATES; ++j)
            {
                //account must be in last epoch of term if this is true
                if(eb.delegates[j].account == request.origin
                        && eb.delegates[j].starting_term)
                {
                    add_to_candidates_db = true;
                    break;
                }
            }
            break;
        }
    }
    if(add_to_candidates_db)
    {
        assert(!_store.candidate_put(request.origin,candidate,txn));
    }
    assert(!_store.request_put(request,txn));
    if(request.set_stake)
    {
        StakingManager::GetInstance()->Stake(
                request.origin,
                info,
                request.stake,
                request.origin,
                request.epoch_num,
                txn);
    }
}

void PersistenceManager<R>::ApplyRequest(
        const RenounceCandidacy& request,
        logos::account_info& info,
        MDB_txn* txn)
{
    assert(txn != nullptr);
    info.governance_subchain_head = request.GetHash();
    CandidateInfo candidate;
    if(!_store.candidate_get(request.origin, candidate, txn))
    {
        candidate.TransitionIfNecessary(request.epoch_num);
        assert(!_store.candidate_mark_remove(request.origin,txn));
        if(request.set_stake)
        {
            candidate.next_stake = request.stake; 
        }
        _store.candidate_put(request.origin,candidate,txn);
    }
    RepInfo rep;
    assert(!_store.rep_get(request.origin, rep, txn));
    rep.candidacy_action_tip = request.GetHash();
    assert(!_store.rep_put(request.origin,rep,txn));
    assert(!_store.request_put(request,txn));
    if(request.set_stake)
    {
        StakingManager::GetInstance()->Stake(
                request.origin,
                info,
                request.stake,
                request.origin,
                request.epoch_num,
                txn); 
    }
}

void PersistenceManager<R>::ApplyRequest(
        const Proxy& request,
        logos::account_info& info,
        MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "PersistenceManager<R>::ApplyRequest (Proxy)"
            << "txn is null";
        trace_and_halt();
    }

    info.governance_subchain_head = request.GetHash();
    if(_store.request_put(request,txn))
    {
        trace_and_halt();
    }
    StakingManager::GetInstance()->Stake(
            request.origin,
            info,
            request.lock_proxy,
            request.rep,
            request.epoch_num,
            txn);
}

void PersistenceManager<R>::ApplyRequest(
        const Stake& request,
        logos::account_info& info,
        MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "PersistenceManager<R>::ApplyRequest (Proxy)"
            << "txn is null";
        trace_and_halt();
    }

    info.governance_subchain_head = request.GetHash();
    if(_store.request_put(request,txn))
    {
        trace_and_halt();
    }

    StakingManager::GetInstance()->Stake(
            request.origin,
            info,
            request.stake,
            request.origin,
            request.epoch_num,
            txn);

    //update candidate stake
    CandidateInfo candidate;
    if(!_store.candidate_get(request.origin,candidate,txn))
    {
        candidate.TransitionIfNecessary(request.epoch_num);
        candidate.next_stake = request.stake; 
        _store.candidate_put(request.origin,candidate,txn);
    }
}


void PersistenceManager<R>::ApplyRequest(
        const Unstake& request,
        logos::account_info& info,
        MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "PersistenceManager<R>::ApplyRequest (Proxy)"
            << "txn is null";
        trace_and_halt();
    }

    info.governance_subchain_head = request.GetHash();
    if(_store.request_put(request,txn))
    {
        trace_and_halt();
    }
    StakingManager::GetInstance()->Stake(
            request.origin,
            info,
            0,
            request.origin,
            request.epoch_num,
            txn);

    //update candidate stake
    CandidateInfo candidate;
    if(!_store.candidate_get(request.origin,candidate,txn))
    {
        candidate.TransitionIfNecessary(request.epoch_num);
        candidate.next_stake = 0; 
        _store.candidate_put(request.origin,candidate,txn);
    }
}

bool VerifyCandidacyActionType(RequestType& type)
{
    return type == RequestType::AnnounceCandidacy
        || type == RequestType::RenounceCandidacy
        || type == RequestType::StopRepresenting;
}

bool VerifyRepActionType(RequestType& type)
{
    return type == RequestType::AnnounceCandidacy
        || type == RequestType::StartRepresenting
        || type == RequestType::StopRepresenting;
}

template <typename T>
bool HasMinGovernanceStake(T & request, MDB_txn* txn)
{
    //verify origin will have enough stake to be candidate after this request is
    //applied
    Amount stake = request.stake;
    if(!request.set_stake)
    {
        StakedFunds cur_stake;
        bool has_stake = StakingManager::GetInstance()->GetCurrentStakedFunds(
                request.origin,
                cur_stake,
                txn);
        if(!has_stake)
        {
            Log log;
            LOG_WARN(log) << "HasMinGovernanceStake - account has no stake. "
                          << "request does not set stake";
            return false;
        }
        stake = cur_stake.amount;
    }
    bool is_announce_candidacy = request.type == RequestType::AnnounceCandidacy;
    return stake >= MIN_REP_STAKE && (!is_announce_candidacy || stake >= MIN_DELEGATE_STAKE);
}

//Returns true if account is a delegate in the next epoch
//This function should not be called inside elections dead period
bool IsDelegateNextEpoch(
        logos::block_store& store,
        AccountAddress const & account,
        MDB_txn* txn)
{
    ApprovedEB epoch;
    store.epoch_get_n(0,epoch,txn);
    for(auto delegate : epoch.delegates)
    {
        if(delegate.account == account)
        {
            return true;
        }
    }
    return false;
}

bool PersistenceManager<R>::ValidateRequest(
    const ElectionVote& vote_request,
    logos::account_info & info,
    uint32_t cur_epoch_num,
    MDB_txn* txn,
    logos::process_return & result)
{
    assert(txn != nullptr);
    if(vote_request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }
    if(cur_epoch_num < EpochVotingManager::START_ELECTIONS_EPOCH
       || !EpochVotingManager::ENABLE_ELECTIONS)
    {
        result.code = logos::process_result::no_elections;
        return false;
    }

    // Verify governance_subchain_prev is correct
    if(!ValidateGovernanceSubchain(vote_request, info, result, txn))
    {
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
    assert(VerifyRepActionType(rep_req->type));
    uint32_t rep_req_epoch = GetEpochNum(rep_req);
    if((rep_req->type == RequestType::StartRepresenting
        || rep_req->type == RequestType::AnnounceCandidacy)
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
            assert(VerifyCandidacyActionType(candidacy_req->type));
            if(candidacy_req->type == RequestType::AnnounceCandidacy)
            {
                if(GetEpochNum(candidacy_req) == cur_epoch_num)
                {
                    result.code = logos::process_result::invalid_candidate;
                    return false;
                }
            }
            else if(GetEpochNum(candidacy_req) < cur_epoch_num) //Renounce
            {
                result.code = logos::process_result::invalid_candidate;
                return false;
            }
        }
    }
    return total <= MAX_VOTES;
}

bool PersistenceManager<R>::ValidateRequest(
        const AnnounceCandidacy& request,
        logos::account_info const & info,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    assert(txn != nullptr);
    //epoch consistency
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }
    //No elections requests during dead period. See comments near IsDeadPeriod()
    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }
    //are elections enabled?
    if(cur_epoch_num < EpochVotingManager::START_ELECTIONS_EPOCH - 1
            || !EpochVotingManager::ENABLE_ELECTIONS)
    {
        result.code = logos::process_result::no_elections;
        return false;
    }

    //verify governance_subchain_prev is correct
    if(!ValidateGovernanceSubchain(request, info, result, txn))
    {
        return false;
    }

    //verify rep status of origin
    RepInfo rep;
    if(!_store.rep_get(request.origin, rep, txn))
    {
        std::shared_ptr<Request> rep_request;
        auto hash = rep.rep_action_tip;
        assert(hash != 0);
        assert(!_store.request_get(hash, rep_request, txn));
        assert(VerifyRepActionType(rep_request->type));
        //already submitted rep action this epoch, reject
        if(GetEpochNum(rep_request) == cur_epoch_num)
        {
            result.code = logos::process_result::pending_rep_action;
            return false;
        }
    }

    //verify candidacy status of origin
    std::shared_ptr<Request> candidacy_req;
    auto hash = rep.candidacy_action_tip;
    if(hash != 0)
    {
        assert(!_store.request_get(hash,candidacy_req,txn));
        assert(VerifyCandidacyActionType(candidacy_req->type));
        //already a candidate, reject
        if(candidacy_req->type == RequestType::AnnounceCandidacy)
        {
            result.code = logos::process_result::already_announced_candidacy;
            return false;
        }
        //already submitted candidacy action this epoch, reject
        else if(GetEpochNum(candidacy_req) == cur_epoch_num) //RenounceCandidacy
        {
            result.code = logos::process_result::pending_candidacy_action;
            return false;
        }
    }

    //verify origin will have enough stake to be candidate and rep
    //after this request is applied
    if(!HasMinGovernanceStake(request,txn))
    {
        result.code = logos::process_result::not_enough_stake;
        return false;
    }
    //verify origin can actually stake amount requested
    return ValidateStake(request,info,result,txn);
}

bool PersistenceManager<R>::ValidateRequest(
        const RenounceCandidacy& request,
        logos::account_info const & info,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    assert(txn != nullptr);
    //are elections enabled?
    if(!EpochVotingManager::ENABLE_ELECTIONS)
    {
        result.code = logos::process_result::no_elections;
        return false;
    }
    //epoch consistency
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }
    //No elections requests during dead period. See comments near IsDeadPeriod()
    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }

    //verify governance_subchain_prev is correct
    if(!ValidateGovernanceSubchain(request, info, result, txn))
    {
        return false;
    }

    //verify rep status
    RepInfo rep;
    if(_store.rep_get(request.origin,rep,txn))
    {
        result.code = logos::process_result::not_a_rep;
        return false;
    }

    //verify candidacy status
    auto hash = rep.candidacy_action_tip;
    if(hash == 0)
    {
        result.code = logos::process_result::never_announced_candidacy;
        return false;
    }
    shared_ptr<Request> candidacy_req;
    assert(!_store.request_get(hash, candidacy_req, txn));
    assert(VerifyCandidacyActionType(candidacy_req->type));
    //if already renounced candidacy, reject
    if(candidacy_req->type == RequestType::RenounceCandidacy)
    {
        result.code = logos::process_result::already_renounced_candidacy;
        return false;
    }
    //if already submitted candidacy action this epoch, reject
    else if(GetEpochNum(candidacy_req) == cur_epoch_num)
    {
        result.code = logos::process_result::pending_candidacy_action;
        return false;
    }

    //verify that origin will still have enough stake to be a rep after this 
    //request is applied
    if(!HasMinGovernanceStake(request,txn))
    {
        result.code = logos::process_result::not_enough_stake;
        return false;
    }
    //verify origin can actually stake amount requested
    return ValidateStake(request,info,result,txn);
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
    assert(txn != nullptr);
    Tip tip;
    BlockHash & hash = tip.digest;
    if(_store.epoch_tip_get(tip,txn))
    {
        LOG_FATAL(_log) << "PersistenceManager<R>::IsDeadPeriod - "
                        << "failed to get epoch_tip";
        trace_and_halt();
    }

    ApprovedEB eb;
    if(_store.epoch_get(hash,eb,txn))
    {
        LOG_FATAL(_log) << "PersistenceManager<R>::IsDeadPeriod - "
                        << "failed to get epoch. hash = " << hash.to_string();
        trace_and_halt();
    }

    return (eb.epoch_number+2) == cur_epoch_num;
}

bool PersistenceManager<R>::ValidateRequest(
        const StartRepresenting& request,
        logos::account_info const & info,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    assert(txn != nullptr);
    //are elections enabled?
    if(!EpochVotingManager::ENABLE_ELECTIONS)
    {
        result.code = logos::process_result::no_elections;
        return false;
    }
    //epoch consistency
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }
    //No elections requests during dead period. See comments near IsDeadPeriod()
    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }

    //verify governance_subchain_prev is correct
    if(!ValidateGovernanceSubchain(request, info, result, txn))
    {
        return false;
    }

    //Verify current rep status of origin
    RepInfo rep;
    if(!_store.rep_get(request.origin,rep,txn))
    {
        auto hash = rep.rep_action_tip;
        assert(hash != 0);
        std::shared_ptr<Request> rep_req;
        assert(!_store.request_get(hash, rep_req, txn));
        assert(VerifyRepActionType(rep_req->type));
        //if already rep, reject
        if(rep_req->type == RequestType::StartRepresenting
                || rep_req->type == RequestType::AnnounceCandidacy)
        {
            result.code = logos::process_result::is_rep;
            return false;
        }
        //if already issued rep action this epoch, reject
        else if(GetEpochNum(rep_req) == cur_epoch_num)
        {
            result.code = logos::process_result::pending_rep_action;
            return false;
        }
    }
    //verify origin either already has enough self stake to be a rep
    //or is requesting to increase self stake above threshold
    if(!HasMinGovernanceStake(request,txn))
    {
        result.code = logos::process_result::not_enough_stake;
        return false;
    }
    //verify origin can actually stake amount requested
    //Note, if an account issues StartRepresenting while that account has locked
    //proxied stake, the locked proxied stake will be redelegated to self (if
    //secondary liabilities allow it) or will begin thawing. Representatives
    //cannot have locked proxied stake 
    return ValidateStake(request,info,result,txn);
}

bool PersistenceManager<R>::ValidateRequest(
        const StopRepresenting& request,
        logos::account_info const & info,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    assert(txn != nullptr);
    //are elections enabled?
    if(!EpochVotingManager::ENABLE_ELECTIONS)
    {
        result.code = logos::process_result::no_elections;
        return false;
    }
    //epoch consistency
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }

    //No elections related requests allowed in dead period
    //See comments around IsDeadPeriod() function
    if(IsDeadPeriod(cur_epoch_num,txn))
    {
        result.code = logos::process_result::elections_dead_period;
        return false;
    }

    //Verify governance_subchain_prev is correct
    if(!ValidateGovernanceSubchain(request, info, result, txn))
    {
        return false;
    }

    //Verify representative and candidacy status of origin
    RepInfo rep;
    if(!_store.rep_get(request.origin,rep,txn))
    {
        auto hash = rep.rep_action_tip;
        assert(hash != 0);
        std::shared_ptr<Request> rep_request;
        assert(!_store.request_get(hash, rep_request, txn));
        assert(VerifyRepActionType(rep_request->type));
        //verify rep status
        if(GetEpochNum(rep_request) == cur_epoch_num)
        {
            //pending rep action in current epoch
            result.code = logos::process_result::pending_rep_action;
            return false;
        }
        else if(rep_request->type == RequestType::StopRepresenting)
        {
            //already stopped representing in previous epoch
            result.code = logos::process_result::not_a_rep;
            return false;
        }

        //verify candidacy status
        //StopRepresenting is invalid if origin previously submitted
        //AnnounceCandidacy, and has not submitted RenounceCandidacy prior
        //to the current epoch
        hash = rep.candidacy_action_tip;
        if(hash != 0)
        {
            std::shared_ptr<Request> candidacy_req;
            assert(!_store.request_get(hash, candidacy_req, txn));
            assert(VerifyCandidacyActionType(candidacy_req->type));
            if(GetEpochNum(candidacy_req) == cur_epoch_num)
            {
                //pending candidacy action in current epoch
                result.code = logos::process_result::pending_candidacy_action;
                return false;
            }
            //Need to submit RenounceCandidacy prior to StopRepresenting
            if(candidacy_req->type == RequestType::AnnounceCandidacy)
            {
                result.code = logos::process_result::is_candidate;
                return false;
            }
        }
    }
    //if origin is not a rep, reject
    else
    {
        result.code = logos::process_result::not_a_rep;
        return false;
    }

    //Verify origin is not a delegate next epoch
    //Delegate must wait until last epoch of term to submit StopRepresenting
    if(IsDelegateNextEpoch(_store, request.origin, txn))
    {
        result.code = logos::process_result::is_delegate;
        return false;
    }
    //Verify origin can stake amount requested
    return ValidateStake(request,info,result,txn);
}

bool PersistenceManager<R>::ValidateRequest(
        const Proxy& request,
        logos::account_info const & info,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "PersistenceManager<R>::ValidateRequest - "
                        << "txn is null";
        trace_and_halt();
    }
    //epoch consistency
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }

    //Verify governance_subchain_prev is correct
    if(!ValidateGovernanceSubchain(request, info, result, txn))
    {
        return false;
    }

    //Cannot proxy to self
    if(request.rep == request.origin)
    {
        result.code = logos::process_result::proxy_to_self;
        return false;
    }

    //Cannot proxy if origin account is already rep (or will be rep next epoch)
    RepInfo rep_info;
    if(!_store.rep_get(request.origin,rep_info,txn))
    {
        auto hash = rep_info.rep_action_tip;
        std::shared_ptr<Request> req;
        if(_store.request_get(hash,req,txn))
        {
            LOG_FATAL(_log) << "PersistenceManager<R>::ValidateRequest (Proxy)"
                            << " - failed to retrieve rep_action_tip"
                            << " hash = " << hash.to_string();
            trace_and_halt();
        }
        //If origin account is resigning as rep, Proxy request is valid
        //otherwise, reject
        if(req->type != RequestType::StopRepresenting)
        {
            result.code = logos::process_result::is_rep;
            return false;
        }
    }

    //Verify origin account is proxying to a valid rep
    if(!_store.rep_get(request.rep,rep_info,txn))
    {
        auto hash = rep_info.rep_action_tip;
        std::shared_ptr<Request> req;
        if(_store.request_get(hash,req,txn))
        {
            LOG_FATAL(_log) << "PersistenceManager<R>::ValidateRequest (Proxy)"
                            << " - failed to retrieve rep_action_tip"
                            << " hash = " << hash.to_string();
            trace_and_halt();
        }
        //request.rep is a current rep, but is not a rep next epoch. Reject
        if(req->type == RequestType::StopRepresenting)
        {
            result.code = logos::process_result::not_a_rep;
            return false;
        }
    }
    else
    {
        //Reject Proxy to account that is not a rep
        result.code = logos::process_result::not_a_rep;
        return false;
    }

    //Verify origin is able to stake (lock proxy) amount requested
    bool can_stake = StakingManager::GetInstance()->Validate(
            request.origin,
            info,
            request.lock_proxy,
            request.rep,
            request.epoch_num,
            request.fee,
            txn);
    if(!can_stake)
    {
        result.code = logos::process_result::insufficient_funds_for_stake;
    }
    return can_stake;
}

bool PersistenceManager<R>::ValidateRequest(
        const Stake& request,
        logos::account_info const & info,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "PersistenceManager<R>::ValidateRequest - "
                        << "txn is null";
        trace_and_halt();
    }
    //epoch consistency
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }

    //Verify governance_subchain_prev is correct
    if(!ValidateGovernanceSubchain(request, info, result, txn))
    {
        return false;
    }

    //Can't submit Stake request if you have a rep already
    if(info.new_rep.rep != 0)
    {
       result.code = logos::process_result::not_a_rep;
       return false; 
    }

    //Verify the desired amount to stake is above Representative and Delegate
    //threshold if necessary
    RepInfo rep_info;
    if(!_store.rep_get(request.origin,rep_info,txn))
    {
        auto hash = rep_info.rep_action_tip;
        std::shared_ptr<Request> req;
        if(_store.request_get(hash,req,txn))
        {
            LOG_FATAL(_log) << "PersistenceManager<R>::ValidateRequest (Stake)"
                            << " - failed to retrieve rep_action_tip"
                            << " hash = " << hash.to_string();
            trace_and_halt();
        }
        //If account is rep next epoch, need to have at least MIN_REP_STAKE
        if(req->type != RequestType::StopRepresenting)
        {
            if(request.stake < MIN_REP_STAKE)
            {
                result.code = logos::process_result::not_enough_stake;
                return false;
            }
        }

        hash = rep_info.candidacy_action_tip;
        if(hash != 0)
        {
            if(_store.request_get(hash, req, txn))
            {
                LOG_FATAL(_log) << "PersistenceManager<R>::ValidateRequest (Stake)"
                                << " - failed to retrieve candidacy_action_tip"
                                << " hash = " << hash.to_string();
                trace_and_halt();
            }
            //If account is candidate next epoch (or will be candidate when up
            //for reelection, in the case of delegate), need to have MIN_DELEGATE_STAKE
            if(req->type == RequestType::AnnounceCandidacy)
            {
                if(request.stake < MIN_DELEGATE_STAKE)
                {
                    result.code = logos::process_result::not_enough_stake;
                    return false;
                }
            }
        }
    }

    //Verify account is able to stake amount requested
    bool can_stake = StakingManager::GetInstance()->Validate(
            request.origin,
            info,
            request.stake,
            request.origin,
            request.epoch_num,
            request.fee,
            txn);
    if(!can_stake)
    {
        result.code = logos::process_result::insufficient_funds_for_stake;
    }
    return can_stake;
}

bool PersistenceManager<R>::ValidateRequest(
        const Unstake& request,
        logos::account_info const & info,
        uint32_t cur_epoch_num,
        MDB_txn* txn,
        logos::process_return& result)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "PersistenceManager<R>::ValidateRequest - "
                        << "txn is null";
        trace_and_halt();
    }
    //epoch consistency
    if(request.epoch_num != cur_epoch_num)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }
    //Verify governance_subchain_prev is correct
    if(!ValidateGovernanceSubchain(request, info, result, txn))
    {
        return false;
    }

    //if account has a rep already, cannot perform Unstake
    //any self staked funds should have began thawing when account selected a rep
    //via Proxy request
    if(info.new_rep.rep != 0)
    {
       result.code = logos::process_result::not_a_rep;
       return false; 
    }

    //Verify account is not a rep next epoch
    //If account is rep next epoch, reject Unstake request
    RepInfo rep_info;
    if(!_store.rep_get(request.origin,rep_info,txn))
    {
        auto hash = rep_info.rep_action_tip;
        std::shared_ptr<Request> req;
        if(_store.request_get(hash,req,txn))
        {
            LOG_FATAL(_log) << "PersistenceManager<R>::ValidateRequest (Proxy)"
                            << " - failed to retrieve rep_action_tip"
                            << " hash = " << hash.to_string();
            trace_and_halt();
        }
        if(req->type != RequestType::StopRepresenting)
        {
            result.code = logos::process_result::not_enough_stake;
            return false;
        }
    }

    return true;
}

Reward PersistenceManager<R>::ProcessClaim(const std::shared_ptr<const Claim> claim,
                                           logos::account_info & info,
                                           MDB_txn * transaction)
{
    std::shared_ptr<Request> current_request;
    BlockHash current_hash = info.governance_subchain_head;
    uint32_t peak = claim->epoch_number;

    bool is_rep = false;
    AccountAddress current_rep = 0;
    Amount staked = 0;

    Rational sum = 0;

    auto advance_chain = [&]()
    {
        current_hash = dynamic_pointer_cast<Governance>(current_request)->governance_subchain_prev;
    };

    // The main loop for processing the claim request.
    for(; !current_hash.is_zero() && peak > info.claim_epoch; advance_chain())
    {
        if(_store.request_get(current_hash, current_request, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager::ProcessClaim - "
                            << "Failed to retrieve governance request with hash: "
                            << current_hash.to_string();
            trace_and_halt();
        }

        auto base = GetEpochNum(current_request);

        // This request may determine how to harvest
        // rewards in epoch base + 1;
        if(base < peak)
        {

            // Update the state of the account to determine
            // how to harvest rewards for epochs in the range
            // [base + 1, peak].
            switch(current_request->type)
            {
                case RequestType::Proxy:
                {
                    auto proxy = dynamic_pointer_cast<Proxy>(current_request);
                    assert(proxy);

                    if(proxy->lock_proxy.is_zero())
                    {
                        current_rep = {0};
                        staked = {0};
                    }
                    else
                    {
                        current_rep = proxy->rep;
                        staked = proxy->lock_proxy;
                    }

                    is_rep = false;

                    break;
                }
                case RequestType::AnnounceCandidacy:
                case RequestType::StartRepresenting:
                    current_rep = {0};
                    is_rep = true;
                    break;
                case RequestType::StopRepresenting:
                    is_rep = false;
                    break;
                case RequestType::Stake:
                case RequestType::Unstake:
                case RequestType::ElectionVote:
                case RequestType::RenounceCandidacy:
                    continue;
                default:
                    LOG_FATAL(_log) << "Unexpected message type encountered in governance subchain:"
                                    << GetRequestTypeField(current_request->type);
                    trace_and_halt();
            }

            bool has_rep = !current_rep.is_zero();

            // The account has a representative and is
            // a representative. Should never occur.
            if(has_rep && is_rep)
            {
                LOG_FATAL(_log) << "PersistenceManager::ProcessClaim - "
                                << "Inconsistent account state while processing "
                                << "claim for account: "
                                << claim->origin.to_account();
                trace_and_halt();
            }

            // There may be rewards to harvest for
            // the epoch.
            if(has_rep || is_rep)
            {

                auto rewards_manager = EpochRewardsManager::GetInstance();

                // Iterate over all the epochs affected by the
                // user's current status.
                for(uint32_t epoch = base + 1; epoch <= peak; ++epoch)
                {

                    // Rewards from this epoch have already
                    // been claimed.
                    if(epoch <= info.claim_epoch)
                    {
                        continue;
                    }

                    auto rep_address = [&]()
                    {
                        if(is_rep)
                        {
                            return claim->origin;
                        }

                        return current_rep;
                    };

                    // This rep participated in voting for the epoch and
                    // is therefore entitled to rewards.
                    if(rewards_manager->RewardsAvailable(rep_address(), epoch, transaction))
                    {
                        auto rep_info = rewards_manager->GetRewardsInfo(rep_address(), epoch, transaction);

                        // This rep's portion of global rewards hasn't
                        // yet been determined.
                        if(!rep_info.initialized)
                        {

                            // There are still global rewards available
                            // to distribute to this rep.
                            if(rewards_manager->GlobalRewardsAvailable(epoch, transaction))
                            {
                                auto global_info = rewards_manager->GetGlobalRewardsInfo(epoch,
                                                                                         transaction);

                                auto rep_pool = Rational(rep_info.total_stake.number(),
                                                         global_info.total_stake.number()) * global_info.total_reward.number();

                                rewards_manager->HarvestGlobalReward(epoch,
                                                                     rep_pool,
                                                                     global_info,
                                                                     transaction);

                                rep_info.total_reward = rep_pool;
                                rep_info.remaining_reward = rep_pool;
                            }

                            else
                            {
                                LOG_FATAL(_log) << "PersistenceManager::ProcessClaim - "
                                                << "No global rewards available for uninitialized "
                                                << "rep reward pool. Rep address: "
                                                << rep_address().to_account();
                                trace_and_halt();
                            }

                            rep_info.initialized = true;
                        }

                        // This manipulates the stake amounts used in determining
                        // rep and locked proxy rewards in order to adhere to the
                        // prescribed levy_percentage.
                        auto self_stake = [&]()
                        {
                            logos::uint256_t hecto = 100;
                            logos::uint256_t result;

                            if(is_rep)
                            {
                                logos::uint256_t factor = hecto - rep_info.levy_percentage;
                                logos::uint256_t proxy_stake = rep_info.total_stake.number() - rep_info.self_stake.number();

                                result = rep_info.self_stake.number() +
                                         ((factor * proxy_stake) / 100);
                            }

                            else
                            {
                                result = (logos::uint256_t(staked.number()) *
                                          logos::uint256_t(rep_info.levy_percentage)) / hecto;
                            }

                            return result.convert_to<logos::uint128_t>();
                        };

                        auto reward = Rational(self_stake(), rep_info.total_stake.number()) * rep_info.total_reward;

                        rewards_manager->HarvestReward(rep_address(),
                                                       epoch,
                                                       reward,
                                                       rep_info,
                                                       transaction);

                        // Finally, update the sum with the
                        // earnings from this epoch.
                        sum += reward;
                    }
                }
            }

            peak = base;
        }
    }

    info.claim_epoch = claim->epoch_number;

    return Reward((sum.numerator() / sum.denominator()).convert_to<logos::uint128_t>(),
                  Rational(sum.numerator() % sum.denominator(),
                           sum.denominator()));
}

bool PersistenceManager<R>::ValidateRequest(
    const Claim & request,
    logos::process_return & result,
    std::shared_ptr<logos::Account> info)
{
    auto user_account = dynamic_pointer_cast<logos::account_info>(info);

    auto epoch = _store.epoch_number_stored();

    if(request.epoch_number > epoch || user_account->claim_epoch >= request.epoch_number)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }

    ApprovedEB eb;
    if(_store.epoch_get(request.epoch_hash, eb))
    {
        result.code = logos::process_result::invalid_epoch_hash;
        return false;
    }

    if(request.epoch_number != eb.epoch_number)
    {
        result.code = logos::process_result::wrong_epoch_number;
        return false;
    }

    return true;
}
