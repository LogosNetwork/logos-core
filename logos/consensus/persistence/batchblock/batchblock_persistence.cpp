/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/lib/trace.hpp>
#include <logos/common.hpp>

constexpr uint128_t PersistenceManager<B>::MIN_TRANSACTION_FEE;

PersistenceManager<B>::PersistenceManager(Store & store,
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

void PersistenceManager<B>::ApplyUpdates(
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

    LOG_DEBUG(_log) << "PersistenceManager<B>::ApplyUpdates - BSB with "
                    << message.requests.size()
                    << " StateBlocks";

    logos::transaction transaction(_store.environment, nullptr, true);
    StoreBatchMessage(message, transaction, delegate_id);
    ApplyBatchMessage(message, transaction);
}

bool PersistenceManager<B>::Validate(
    const Request & block,
    logos::process_return & result,
    bool allow_duplicates)
{
    auto hash = block.GetHash();

    if(block.account.is_zero())
    {
        result.code = logos::process_result::opened_burn_account;
        return false;
    }

    if(block.transaction_fee.number() < MIN_TRANSACTION_FEE)
    {
        result.code = logos::process_result::insufficient_fee;
        return false;
    }

    std::lock_guard<std::mutex> lock(_reservation_mutex);

    logos::account_info info;
    auto account_error(_reservations->Acquire(block.account, info));

    // Account exists.
    if(!account_error)
    {
        //sequence number
        if(info.block_count != block.sequence)
        {
            result.code = logos::process_result::wrong_sequence_number;
            LOG_INFO(_log) << "wrong_sequence_number, request sqn="<<block.sequence
                           << " expecting=" << info.block_count;
            return false;
        }
        // No previous block set.
        if(block.previous.is_zero() && info.block_count)
        {
            result.code = logos::process_result::fork;
            return false;
        }

        // This account has issued at least one send transaction.
        if(info.block_count)
        {
            if(!_store.request_exists(block.previous))
            {
                result.code = logos::process_result::gap_previous;
                BOOST_LOG (_log) << "GAP_PREVIOUS: cannot find previous hash " << block.previous.to_string()
                                 << "; current account info head is: " << info.head.to_string();
                return false;
            }
        }

        if(block.previous != info.head)
        {
            // Allow duplicate requests (hash == info.head)
            // received from batch blocks.
            if(hash == info.head)
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

        // Have we seen this block before?
        if(_store.request_exists(hash))
        {
            result.code = logos::process_result::old;
            return false;
        }

        // TODO
        uint32_t current_epoch = 0;

        auto update_reservation = [&info, &hash, current_epoch]()
                                  {
                                       info.reservation = hash;
                                       info.reservation_epoch = current_epoch;
                                  };

        // Account is not reserved.
        if(info.reservation.is_zero())
        {
            update_reservation();
        }

        // Account is already reserved.
        else if(info.reservation != hash)
        {
            // This block conflicts with existing reservation.
            if(current_epoch < info.reservation_epoch + RESERVATION_PERIOD)
            {
                result.code = logos::process_result::already_reserved;
                return false;
            }

            // Reservation has expired.
            update_reservation();
        }

        auto total = block.transaction_fee.number();
        for(auto & i : block.transactions)
        {
            total += i.amount.number();
        }
        if(total > info.balance.number())
        {
            result.code = logos::process_result::insufficient_balance;
            return false;
        }
    }

    // account doesn't exist
    else
    {
        // Currently do not accept state blocks
        // with non-existent accounts.
        result.code = logos::process_result::unknown_source_account;
        return false;

        if(!block.previous.is_zero())
        {
            return false;
        }
    }

    result.code = logos::process_result::progress;
    return true;
}

bool PersistenceManager<B>::Validate(
    const Request & block)
{
    logos::process_return ignored_result;
    auto re = Validate(block, ignored_result);
    LOG_DEBUG(_log) << "PersistenceManager<B>::Validate code " << (uint)ignored_result.code;
    return re;
}

bool PersistenceManager<B>::Validate(
    const PrePrepare & message,
    ValidationStatus * status)
{
    using namespace logos;

    bool valid = true;
    for(uint64_t i = 0; i < message.requests.size(); ++i)
    {
        logos::process_return   result;
        if(!Validate(static_cast<const Request&>(*message.requests[i]), result))
        {
            UpdateStatusRequests(status, i, result.code);
            UpdateStatusReason(status, process_result::invalid_request);
            valid = false;
        }
    }

    return valid;
}

void PersistenceManager<B>::StoreBatchMessage(
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

void PersistenceManager<B>::ApplyBatchMessage(
    const ApprovedBSB & message,
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
void PersistenceManager<B>::ApplyStateMessage(
    const Send & request,
    uint64_t timestamp,
    MDB_txn * transaction)
{
    if(!UpdateSourceState(request, transaction))
    {
        UpdateDestinationState(request, timestamp, transaction);
    }
}

bool PersistenceManager<B>::UpdateSourceState(
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
                   request.transaction_fee.number();

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

void PersistenceManager<B>::UpdateDestinationState(
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
        auto account_error(_store.account_get(t.target, info));

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
                            << t.target.to_string();
        }

        info.receive_count++;
        info.receive_head = hash;
        info.balance = info.balance.number() + t.amount.number();
        info.modified = logos::seconds_since_epoch();

        if(_store.account_put(t.target, info, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
                            << "Failed to store account: "
                            << t.target.to_string();

            std::exit(EXIT_FAILURE);
        }

        PlaceReceive(receive, timestamp, transaction);
    }
}

// TODO: Discuss total order of receives in
//       receive_db of all nodes.
void PersistenceManager<B>::PlaceReceive(
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

            ApprovedBSB approved;
            if(! _store.batch_block_get(send.batch_hash, approved, transaction))
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
