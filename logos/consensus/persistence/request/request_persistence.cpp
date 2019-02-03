/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/consensus_container.hpp>
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
    const ApprovedBSB & message,
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

    LOG_DEBUG(_log) << "PersistenceManager<R>::ApplyUpdates - BSB with "
                    << message.requests.size()
                    << " StateBlocks";

    // SYL integration: need to ensure the operations below execute atomically
    // Otherwise, multiple calls to batch persistence may overwrite balance for the same account
    std::lock_guard<std::mutex> lock (_write_mutex);
    {
        logos::transaction transaction(_store.environment, nullptr, true);
        StoreBatchMessage(message, transaction, delegate_id);
        ApplyBatchMessage(message, transaction);
    }
    // SYL Integration: clear reservation AFTER flushing to LMDB to ensure safety
    for(uint16_t i = 0; i < message.block_count; ++i)
    {
        _reservations->Release(message.blocks[i]->account);
    }
}

bool PersistenceManager<R>::BlockExists(
    const ApprovedBSB & message)
{
    return _store.batch_block_exists(message);
}


bool PersistenceManager<R>::Validate(
    std::shared_ptr<const Request> request,
    logos::process_return & result,
    bool allow_duplicates,
    bool prelim)
{
    // SYL Integration: move signature validation here so we always check
    if(ConsensusContainer::ValidateSigConfig() && ! request.VerifySignature(request.account))
    {
        LOG_WARN(_log) << "PersistenceManager<R> - Validate, bad signature: "
                       << request.signature.to_string()
                       << " account: " << request.account.to_string();

        result.code = logos::process_result::bad_signature;
        return false;
    }

    auto hash = request.GetHash();

    // burn account and transaction fee validation is done in TxAcceptor

    // SYL Integration: remove _reservation_mutex for now and rely on coarser _write_mutex. Potential fix later
    logos::account_info info;
    // account doesn't exist
    if (_store.account_get(request.account, info))
    {
        // Currently do not accept state blocks
        // with non-existent accounts.
        result.code = logos::process_result::unknown_source_account;
        return false;
    }

    // a valid (non-expired) reservation exits
    if (!_reservations->CanAcquire(request.account, hash, allow_duplicates))
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
    if(info.block_count != request.sequence)
    {
        result.code = logos::process_result::wrong_sequence_number;
        LOG_INFO(_log) << "wrong_sequence_number, request sqn=" << request.sequence
                << " expecting=" << info.block_count;
        return false;
    }

    // No previous block set.
    if(request.previous.is_zero() && info.block_count)
    {
        result.code = logos::process_result::fork;
        return false;
    }

    // This account has issued at least one send transaction.
    if(info.block_count)
    {
        if(!_store.state_block_exists(request.previous))
        {
            result.code = logos::process_result::gap_previous;
            LOG_WARN (_log) << "GAP_PREVIOUS: cannot find previous hash " << block.previous.to_string()
                            << "; current account info head is: " << info.head.to_string();
            return false;
        }
    }

    if(request.previous != info.head)
    {
        LOG_WARN (_log) << "PersistenceManager::Validate - discrepancy between block previous hash (" << block.previous.to_string()
                        << ") and current account info head (" << info.head.to_string() << ")";

        // Allow duplicate requests (either hash == info.head or hash matches a transaction further up in the chain)
        // received from batch blocks.
        if(hash == info.head || _store.state_block_exists(hash))
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

                    // This token id doesn't exist.
                    if(_store.token_account_exists(send_tokens->token_id))
                    {
                        result.code = logos::process_result::invalid_token_id;
                        return false;
                    }
                }

                if(!request->Validate(result, info))
                {
                    return false;
                }
            }
        }

    }

    result.code = logos::process_result::progress;
    return true;
}

// Use this for single transaction (non-batch) validation from RPC
bool PersistenceManager<R>::ValidateSingleRequest(
        const Request & block, logos::process_return & result, bool allow_duplicates)
{
    std::lock_guard<std::mutex> lock(_write_mutex);
    return ValidateRequest(block, result, allow_duplicates, false);
}

// Use this for batched transactions validation (either PrepareNextBatch or backup validation)
bool PersistenceManager<R>::ValidateAndUpdate(
        const Request & block, logos::process_return & result, bool allow_duplicates)
{
    auto success (ValidateRequest(block, result, allow_duplicates, false));
    if (success)
    {
        _reservations->UpdateReservation(block.GetHash(), block.origin);
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
        if(!ValidateAndUpdate(static_cast<const Request&>(*message.blocks[i]), ignored_result, true) || bool(message.blocks[i].hash().number() & 1))
#else
        if(!ValidateAndUpdate(static_cast<const Request&>(*message.requests[i]), ignored_result, true))
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

bool PersistenceManager<R>::Validate(
    const PrePrepare & message,
    ValidationStatus * status)
{
    using namespace logos;

    bool valid = true;
    std::lock_guard<std::mutex> lock (_write_mutex);
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        logos::process_return   result;
        if(!ValidateRequest(static_cast<const Request&>(*message.requests[i]), result, true, false))
        {
            UpdateStatusRequests(status, i, result.code);
            UpdateStatusReason(status, process_result::invalid_request);

            valid = false;
        }
    }

    return valid;
}

void PersistenceManager<R>::StoreBatchMessage(
    const ApprovedBSB & message,
    MDB_txn * transaction,
    uint8_t delegate_id)
{
    auto hash(message.Hash());
    LOG_DEBUG(_log) << "PersistenceManager::StoreBatchMessage - "
                    << message.Hash().to_string();

    if(_store.batch_block_put(message, hash, transaction))
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
    const ApprovedBSB & message,
    MDB_txn * transaction)
{
    for(uint16_t i = 0; i < message.requests.size(); ++i)
    {
        auto request = static_pointer_cast<Send>(message.requests[i]);
        ApplyStateMessage(*request,
                          message.timestamp,
                          transaction);
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
    auto account_error(_store.account_get(transaction, request.account, info));

    if(account_error)
    {
        LOG_ERROR (_log) << "PersistenceManager::UpdateSourceState - Unable to find account.";
        return true;
    }

    auto hash = request.GetHash();

    // This can happen when a duplicate request
    // is accepted. We can ignore this transaction.
    if(request.previous != info.head)
    {
        if(hash == info.head || _store.state_block_exists(hash))
        {
            LOG_INFO(_log) << "PersistenceManager::UpdateSourceState - Block hash: "
                           << hash.to_string()
                           << ", account head: "
                           << info.head.to_string()
                           << " - Suspected duplicate request - "
                           << "ignoring old block.";
            return true;
        }
        // Somehow a fork slipped through
        else
        {
            LOG_FATAL(_log) << "PersistenceManager::UpdateSourceState - encountered fork with hash "
                            << hash.to_string();
            trace_and_halt();
        }
    }

    info.block_count++;
    info.balance = info.balance.number() -
                   request.fee.number();

    for(auto & t : request.transactions)
    {
        info.balance = info.balance.number() - t.amount.number();
    }

    info.head = hash;
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
    // SYL: we don't need to lock destination mutex here because updates to same account within
    // the same transaction handle will be serialized, and a lock here wouldn't do anything to
    // prevent race condition across transactions, since flushing to DB is delayed
    // (only when transaction destructor is called)
    uint16_t index2send = 0;
    for(auto & t : request.transactions)
    {
        logos::account_info info;
        auto account_error(_store.account_get(transaction, t.destination, info));

        ReceiveBlock receive(
                /* Previous   */ info.receive_head,
                /* send_hash  */ request.GetHash(),
                /* index2send */ index2send++
        );

        auto hash(receive.Hash());

        // Destination account doesn't exist yet
        if(account_error)
        {
            info.open_block = request.GetHash();
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
                LOG_FATAL(_log) << "PersistenceManager::PlaceReceive - "
                                << "Failed to get a previous state block with hash: "
                                << b.send_hash.to_string();
                trace_and_halt();
            }

            ApprovedBSB approved;
            if(! _store.batch_block_get(send.batch_hash, approved, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::PlaceReceive - "
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
            Send prev_send;
            if(_store.request_get(prev.send_hash, prev_send, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager<B>::PlaceReceive - "
                                << "Failed to get a previous state block with hash: "
                                << prev.send_hash.to_string();
                trace_and_halt();
            }
            if(!prev_send.account.is_zero())
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
