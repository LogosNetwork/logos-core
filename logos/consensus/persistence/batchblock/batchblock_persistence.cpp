/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/lib/trace.hpp>
#include <logos/common.hpp>
#include <mutex>

static std::mutex global_mutex; // RGD

constexpr uint128_t PersistenceManager<BSBCT>::MIN_TRANSACTION_FEE;

PersistenceManager<BSBCT>::PersistenceManager(Store & store,
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

void PersistenceManager<BSBCT>::ApplyUpdates(
    const ApprovedBSB & message,
    uint8_t delegate_id)
{
    // XXX - Failure during any of the database operations
    //       performed in the following two methods will cause
    //       the application to exit without committing the
    //       intermediate transactions to the database.
    std::lock_guard<std::mutex> lock(global_mutex);

    auto batch_hash = message.Hash();
    uint16_t count = 0;
    for(uint16_t i = 0; i < message.block_count; ++i)
    {
        message.blocks[i].batch_hash = batch_hash;
        message.blocks[i].index_in_batch = count++;
    }

    LOG_DEBUG(_log) << "PersistenceManager<BSBCT>::ApplyUpdates - BSB with "
            << message.block_count << " StateBlocks";

    logos::transaction transaction(_store.environment, nullptr, true);
    StoreBatchMessage(message, transaction, delegate_id);
    ApplyBatchMessage(message, transaction);
}

std::atomic<int> EXPECTING;

bool PersistenceManager<BSBCT>::Validate(
    const Request & block,
    logos::process_return & result,
    bool allow_duplicates)
{
    std::lock_guard<std::mutex> glock(global_mutex);

#if 0
    std::cout << "rgd_received: " << block.sequence << std::endl;
    {
    logos::account_info info;
    auto account_error(_reservations->Acquire(block.account, info));

    EXPECTING = 0;
    // Account exists.
    if(!account_error)
    {
        //sequence number
        if(info.block_count != block.sequence)
        {
            std::cout << "wrong_sequence_number, request sqn="<<block.sequence
                    << " expecting=" << info.block_count << std::endl;
            EXPECTING = info.block_count;
            //return false;
        }
    }
    }
#endif
    result.code = logos::process_result::progress; // TODO Hack
    return true;

    auto hash = block.GetHash();
    std::cout << "PersistenceManager::Validate: hash: " << hash.to_string() << std::endl;

    if(block.account.is_zero())
    {
        result.code = logos::process_result::opened_burn_account;
        std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
        return false;
    }

    if(block.transaction_fee.number() < MIN_TRANSACTION_FEE)
    {
        result.code = logos::process_result::insufficient_fee;
        std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
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
            //LOG_INFO(_log) << "wrong_sequence_number, request sqn="<<block.sequence
            std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
            std::cout << "wrong_sequence_number, request sqn="<<block.sequence
                    << " expecting=" << info.block_count << " hash: " << hash.to_string() << std::endl;
            return false; // TODO Hack
        }
        // No previous block set.
        if(block.previous.is_zero() && info.block_count)
        {
            result.code = logos::process_result::fork;
            LOG_DEBUG(_log) << "PersistenceManager:: previous is zero: block count: " << info.block_count << std::endl;
            std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
            return false;
        }

        // This account has issued at least one send transaction.
        if(info.block_count)
        {
            if(!_store.state_block_exists(block.previous))
            {
                result.code = logos::process_result::gap_previous;
                BOOST_LOG (_log) << "GAP_PREVIOUS: cannot find previous hash " << block.previous.to_string()
                                 << "; current account info head is: " << info.head.to_string();
                std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
                return false; // TODO Hack
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
                    std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
                    return true;
                }
                else
                {
                    result.code = logos::process_result::old;
                    std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
                    return false;
                }
            }
            else
            {
                result.code = logos::process_result::fork;
                LOG_DEBUG(_log) << "block.hashables.previous: " << block.previous.to_string()
                                << " info.head: " << info.head.to_string() 
                                << " hash: " << hash.to_string() << std::endl;
                std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
                return false; // RGD Hack
            }
        }

        // Have we seen this block before?
        if(_store.state_block_exists(hash))
        {
            result.code = logos::process_result::old;
            std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
            return false; // RGD Hack
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
                std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
                return false;
            }

            // Reservation has expired.
            update_reservation();
        }

        auto total = block.transaction_fee.number();
        for(auto & i : block.trans)
        {
            total += i.amount.number();
        }
        if(total > info.balance.number())
        {
            result.code = logos::process_result::insufficient_balance;
            std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
            return false;
        }
    }

    // account doesn't exist
    else
    {
        // Currently do not accept state blocks
        // with non-existent accounts.
        result.code = logos::process_result::unknown_source_account;
        std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
        return false;

        if(!block.previous.is_zero())
        {
            std::cout << "PersistenceManager::Validate: line: " << __LINE__ << std::endl;
            return false;
        }
    }

    std::cout << "PersistenceManager::Validate: line: " << __LINE__ << " success: " << hash.to_string() << std::endl;
    result.code = logos::process_result::progress;
    return true;
}

bool PersistenceManager<BSBCT>::Validate(
    const Request & block)
{
    logos::process_return ignored_result;
    auto re = Validate(block, ignored_result);
    LOG_DEBUG(_log) << "PersistenceManager<BSBCT>::Validate code " << (uint)ignored_result.code;
    return re;
}

bool PersistenceManager<BSBCT>::Validate(
    const PrePrepare & message,
    ValidationStatus * status)
{
    using namespace logos;

    bool valid = true;
    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        logos::process_return   result;
        if(!Validate(static_cast<const Request&>(message.blocks[i]), result))
        {
            UpdateStatusRequests(status, i, result.code);
            UpdateStatusReason(status, process_result::invalid_request);
            valid = false;
        }
    }

    return valid;
}

void PersistenceManager<BSBCT>::StoreBatchMessage(
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
        if(_store.consensus_block_update_next(message.previous, hash, ConsensusType::BatchStateBlock, transaction))
        {
            // TODO: bootstrap here.
        }
    }

    // TODO: Add previous hash for batch blocks with
    //       a previous set to zero because it was
    //       the first batch of the epoch.
}

void PersistenceManager<BSBCT>::ApplyBatchMessage(
    const ApprovedBSB & message,
    MDB_txn * transaction)
{
    for(uint16_t i = 0; i < message.block_count; ++i)
    {
        ApplyStateMessage(message.blocks[i],
                          message.timestamp,
                          transaction);

        std::lock_guard<std::mutex> lock(_reservation_mutex);
        _reservations->Release(message.blocks[i].account);
    }
}

// Currently designed only to handle
// send transactions.
void PersistenceManager<BSBCT>::ApplyStateMessage(
    const StateBlock & block,
    uint64_t timestamp,
    MDB_txn * transaction)
{
    if(!UpdateSourceState(block, transaction))
    {
        UpdateDestinationState(block, timestamp, transaction);
    }
}

bool PersistenceManager<BSBCT>::UpdateSourceState(
    const StateBlock & block,
    MDB_txn * transaction)
{
    logos::account_info info;
    auto account_error(_store.account_get(block.account, info));

    if(account_error)
    {
        LOG_ERROR (_log) << "PersistenceManager::UpdateSourceState - Unable to find account.";
        return true;
    }

    // This can happen when a duplicate request
    // is accepted. We can ignore this transaction.
    if(block.previous != info.head)
    {
#if 0 // RGD It crashed here...
        LOG_INFO(_log) << "Block previous ("
                       << block.previous.to_string()
                       << ") does not match account head ("
                       << info.head.to_string()
                       << "). Suspected duplicate request - "
                       << "ignoring.";
#endif
        return true;
    }

    info.block_count++;
    info.balance = info.balance.number() -
                   block.transaction_fee.number();

    for(auto & t : block.trans)
    {
        info.balance = info.balance.number() - t.amount.number();
    }

    info.head = block.GetHash();
    info.modified = logos::seconds_since_epoch();

    if(_store.account_put(block.account, info, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::UpdateSourceState - "
                        << "Failed to store account: "
                        << block.account.to_string();

        std::cout << " EXIT_FAILURE<1> " << std::endl;
        std::exit(EXIT_FAILURE);
    }

    return false;
}

void PersistenceManager<BSBCT>::UpdateDestinationState(
    const StateBlock & block,
    uint64_t timestamp,
    MDB_txn * transaction)
{
    // Protects against a race condition concerning
    // simultaneous receives for the same account.
    //
    std::lock_guard<std::mutex> lock(_destination_mutex);
    uint16_t index2send = 0;
    for(auto & t : block.trans)
    {
        logos::account_info info;
        auto account_error(_store.account_get(t.target, info));

        ReceiveBlock receive(
                /* Previous  */ info.receive_head,
                /* send_hash */ block.GetHash(),
                /* index2send*/ index2send++
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

            std::cout << " EXIT_FAILURE<2> " << std::endl;
            //std::exit(EXIT_FAILURE); // RGD
        }

        PlaceReceive(receive, timestamp, transaction);
    }
}


//TODO discuss, total order of receives in receive_db of all nodes
void PersistenceManager<BSBCT>::PlaceReceive(
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
            //need b's timestamp
            StateBlock sb;
            if(_store.state_block_get(b.send_hash, sb, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::<BSBCT>::PlaceReceive - "
                                        << "Failed to get a previous state block with hash: "
                                        << b.send_hash.to_string();
                trace_and_halt();
            }

            ApprovedBSB absb;
            if(_store.batch_block_get(sb.batch_hash, absb, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager::PersistenceManager<BSBCT>::PlaceReceive - "
                                        << "Failed to get a previous batch state block with hash: "
                                        << sb.batch_hash.to_string();
                trace_and_halt();
            }

            auto timestamp_b = absb.timestamp;
            bool a_is_less;
            if(timestamp_a != timestamp_b)
            {
                a_is_less = timestamp_a < timestamp_b;
            }else
            {
                a_is_less = a.Hash() < b.Hash();
            }

            timestamp_a = timestamp_b;//update for next compare if needed
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
                    LOG_FATAL(_log) << "PersistenceManager<BSBCT>::PlaceReceive - "
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
            receive.previous = prev.previous;
            prev.previous = hash;
            StateBlock sb_prev;
            if(_store.state_block_get(prev.send_hash, sb_prev, transaction))
            {
                LOG_FATAL(_log) << "PersistenceManager<BSBCT>::PlaceReceive - "
                                << "Failed to get a previous state block with hash: "
                                << prev.send_hash.to_string();
                trace_and_halt();
            }
            if(!sb_prev.account.is_zero())
            {
                // point following receive aka prev's 'previous' field to new receive
                receive.previous = prev.previous;
                prev.previous = hash;
                auto prev_hash (prev.Hash());
                if(_store.receive_put(prev_hash, prev, transaction))
                {
                    LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
                                    << "Failed to store receive block with hash: "
                                    << prev_hash.to_string();

                    trace_and_halt();
                }
            }
            else  // sending to burn address is already prohibited
            {
                LOG_FATAL(_log) << "PersistenceManager<BSBCT>::PlaceReceive - "
                                << "Encountered state block with empty account field, hash: "
                                << prev.send_hash.to_string();
                trace_and_halt();
            }
        }
    }
    else if (!receive.previous.is_zero())
    {
        LOG_FATAL(_log) << "PersistenceManager<BSBCT>::PlaceReceive - "
                        << "Failed to get a previous receive block with hash: "
                        << receive.previous.to_string();
        trace_and_halt();
    }

    if(_store.receive_put(hash, receive, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
                        << "Failed to store receive block with hash: "
                        << hash.to_string();

        trace_and_halt();
    }
}
