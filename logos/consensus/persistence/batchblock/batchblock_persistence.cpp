/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/persistence/state_block_locator.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/lib/trace.hpp>
#include <logos/common.hpp>

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
    const PrePrepare & message,
    uint8_t delegate_id)
{
    // XXX - Failure during any of the database operations
    //       performed in the following two methods will cause
    //       the application to exit without committing the
    //       intermediate transactions to the database.
    logos::transaction transaction(_store.environment, nullptr, true);

    StoreBatchMessage(message, transaction, delegate_id);
    ApplyBatchMessage(message, transaction);
}

bool PersistenceManager<BSBCT>::Validate(
    const Request & block,
    logos::process_return & result,
    bool allow_duplicates)
{
    auto hash = block.hash();

    if(block.hashables.account.is_zero())
    {
        result.code = logos::process_result::opened_burn_account;
        return false;
    }

    if(block.hashables.transaction_fee.number() < MIN_TRANSACTION_FEE)
    {
        result.code = logos::process_result::insufficient_fee;
        return false;
    }

    std::lock_guard<std::mutex> lock(_reservation_mutex);

    logos::account_info info;
    auto account_error(_reservations->Acquire(block.hashables.account, info));

    // Account exists.
    if(!account_error)
    {
        // No previous block set.
        if(block.hashables.previous.is_zero() && info.block_count)
        {
            result.code = logos::process_result::fork;
            LOG_DEBUG(_log) << "PersistenceManager:: previous is zero: block count: " << info.block_count << std::endl;
            return false;
        }

        // This account has issued at least one send transaction.
        if(info.block_count)
        {
            if(!_store.state_block_exists(block.hashables.previous))
            {
                result.code = logos::process_result::gap_previous;
                BOOST_LOG (_log) << "GAP_PREVIOUS: cannot find previous hash " << block.hashables.previous.to_string()
                                 << "; current account info head is: " << info.head.to_string();
                return false;
            }
        }

        if(block.hashables.previous != info.head)
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
                LOG_DEBUG(_log) << "block.hashables.previous: " << block.hashables.previous.to_string() 
                                << " info.head: " << info.head.to_string() 
                                << " hash: " << hash.to_string() << std::endl;
                return false;
            }
        }

        // Have we seen this block before?
        if(_store.state_block_exists(hash))
        {
            result.code = logos::process_result::old;
            return false;
        }

        // TODO
        uint64_t current_epoch = 0;

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

        if(block.hashables.amount.number() + block.hashables.transaction_fee.number()
                > info.balance.number())
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

        if(!block.hashables.previous.is_zero())
        {
            return false;
        }
    }

    result.code = logos::process_result::progress;
    return true;
}

bool PersistenceManager<BSBCT>::Validate(
    const Request & block)
{
    logos::process_return ignored_result;
    return Validate(block, ignored_result);
}

bool PersistenceManager<BSBCT>::Validate(
    const PrePrepare & message,
    uint8_t remote_delegate_id,
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
    const BatchStateBlock & message,
    MDB_txn * transaction,
    uint8_t delegate_id)
{
    BatchStateBlock prev;
    bool has_prev = true;

    if(_store.batch_block_get(message.previous, prev, transaction))
    {
        // TODO: bootstrap here.
        //
        if(!message.previous.is_zero())
        {
            LOG_FATAL(_log) << "PersistenceManager::StoreBatchMessage - "
                            << "Failed to find previous: "
                            << message.previous.to_string();

            std::exit(EXIT_FAILURE);
        }
        else
        {
            has_prev = false;
        }
    }

    auto hash(message.Hash());

    if(_store.batch_block_put(message, hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::StoreBatchMessage - "
                        << "Failed to store batch message with hash: "
                        << hash.to_string();

        std::exit(EXIT_FAILURE);
    }

    prev.next = hash;

    // TODO: Add previous hash for batch blocks with
    //       a previous set to zero because it was
    //       the first batch of the epoch.
    if(has_prev)
    {
        if(_store.batch_block_put(prev, message.previous, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager::StoreBatchMessage - "
                            << "Failed to store batch message with hash: "
                            << message.previous.to_string();

            std::exit(EXIT_FAILURE);
        }

    }

    StateBlockLocator locator_template {hash, 0};

    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        locator_template.index = i;
        if(_store.state_block_put(message.blocks[i],
                                  locator_template,
                                  transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager::StoreBatchMessage - "
                            << "Failed to store state block with hash: "
                            << message.blocks[i].hash().to_string();

            std::exit(EXIT_FAILURE);
        }
    }

    if(_store.batch_tip_put(delegate_id, hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::StoreBatchMessage - "
                        << "Failed to store batch block tip with hash: "
                        << hash.to_string();

        std::exit(EXIT_FAILURE);
    }
}

void PersistenceManager<BSBCT>::ApplyBatchMessage(
    const BatchStateBlock & message,
    MDB_txn * transaction)
{
    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        ApplyStateMessage(message.blocks[i],
                          message.timestamp,
                          transaction);

        std::lock_guard<std::mutex> lock(_reservation_mutex);
        _reservations->Release(message.blocks[i].hashables.account);
    }
}

// Currently designed only to handle
// send transactions.
void PersistenceManager<BSBCT>::ApplyStateMessage(
    const logos::state_block & block,
    uint64_t timestamp,
    MDB_txn * transaction)
{
    if(!UpdateSourceState(block, transaction))
    {
        UpdateDestinationState(block, timestamp, transaction);
    }
}

bool PersistenceManager<BSBCT>::UpdateSourceState(
    const logos::state_block & block,
    MDB_txn * transaction)
{
    logos::account_info info;
    auto account_error(_store.account_get(block.hashables.account, info));

    if(account_error)
    {
        LOG_ERROR (_log) << "PersistenceManager::UpdateSourceState - Unable to find account.";
        return true;
    }

    // This can happen when a duplicate request
    // is accepted. We can ignore this transaction.
    if(block.hashables.previous != info.head)
    {
        LOG_INFO(_log) << "Block previous ("
                       << block.hashables.previous.to_string()
                       << ") does not match account head ("
                       << info.head.to_string()
                       << "). Suspected duplicate request - "
                       << "ignoring.";
        return true;
    }

    info.block_count++;
    info.balance = info.balance.number() -
                   block.hashables.amount.number() -
                   block.hashables.transaction_fee.number();
    info.head = block.hash();
    info.modified = logos::seconds_since_epoch();

    if(_store.account_put(block.hashables.account, info, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::UpdateSourceState - "
                        << "Failed to store account: "
                        << block.hashables.account.to_account();

        std::exit(EXIT_FAILURE);
    }

    return false;
}

void PersistenceManager<BSBCT>::UpdateDestinationState(
    const logos::state_block & block,
    uint64_t timestamp,
    MDB_txn * transaction)
{
    // Protects against a race condition concerning
    // simultaneous receives for the same account.
    //
    std::lock_guard<std::mutex> lock(_destination_mutex);

    logos::account_info info;
    auto account_error(_store.account_get(block.hashables.link, info));

    logos::state_block receive(
            /* Account   */ logos::account(block.hashables.link),
            /* Previous  */ info.receive_head,
            /* Rep       */ 0,
            /* Amount    */ block.hashables.amount,
            /* Amount    */ block.hashables.transaction_fee,
            /* Link      */ block.hash(),
            /* Priv Key  */ logos::raw_key(),
            /* Pub Key   */ logos::public_key(),
            /* Work      */ 0,
            /* Timestamp */ timestamp
    );

    auto hash(receive.hash());

    // Destination account doesn't exist yet
    if(account_error)
    {
        info.open_block = hash;
    }

    info.receive_count++;
    info.receive_head = hash;
    info.balance = info.balance.number() + block.hashables.amount.number();
    info.modified = logos::seconds_since_epoch();

    if(_store.account_put(logos::account(block.hashables.link),
                          info, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::UpdateDestinationState - "
                        << "Failed to store account: "
                        << block.hashables.link.to_account();

        std::exit(EXIT_FAILURE);
    }

    PlaceReceive(receive, transaction);
}

void PersistenceManager<BSBCT>::PlaceReceive(
    logos::state_block & receive,
    MDB_txn * transaction)
{
    logos::state_block prev;
    logos::state_block cur;

    auto hash = receive.hash();

    if(!_store.state_block_get(receive.hashables.previous, cur, transaction))
    {
        // Returns true if 'a' should precede 'b'
        // in the receive chain.
        auto receive_cmp = [](const logos::state_block & a,
                              const logos::state_block & b)
                              {
                                  if(a.timestamp != b.timestamp)
                                  {
                                      return a.timestamp < b.timestamp;
                                  }

                                  return a.hash() < b.hash();
                              };

        while(receive_cmp(receive, cur))
        {
            prev = cur;
            if(!_store.state_block_get(cur.hashables.previous,
                                       cur,
                                       transaction))
            {
                break;
            }
        }

        if(!prev.hashables.account.is_zero())
        {
            std::memcpy(receive.hashables.previous.bytes.data(),
                        prev.hashables.previous.bytes.data(),
                        sizeof(receive.hashables.previous.bytes));

            prev.hashables.previous = hash;
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