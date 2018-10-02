#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/persistence/state_block_locator.hpp>
#include <logos/common.hpp>

PersistenceManager::PersistenceManager(Store & store)
    : _reservations(store)
    , _store(store)
{}

void PersistenceManager::ApplyUpdates(const BatchStateBlock & message, uint8_t delegate_id)
{
    logos::transaction transaction(_store.environment, nullptr, true);

    StoreBatchMessage(message, transaction);
    ApplyBatchMessage(message, delegate_id, transaction);
}

bool PersistenceManager::Validate(const logos::state_block & block, logos::process_return & result, uint8_t delegate_id)
{
    auto hash = block.hash();

    // Have we seen this block before?
    if(_store.state_block_exists(hash))
    {
        result.code = logos::process_result::old;
        return false;
    }

    if(block.hashables.account.is_zero())
    {
        result.code = logos::process_result::opened_burn_account;
        return false;
    }

    std::lock_guard<std::mutex> lock(_reservation_mutex);

    logos::account_info info;
    auto account_error(_reservations.Acquire(block.hashables.account, info));

    // Account exists.
    if(!account_error)
    {
        // No previous block set.
        if(block.hashables.previous.is_zero() && info.block_count)
        {
            result.code = logos::process_result::fork;
            return false;
        }

        // This account has issued at least one send transaction.
        if(info.block_count)
        {
            if(!_store.state_block_exists(block.hashables.previous))
            {
                result.code = logos::process_result::gap_previous;
                return false;
            }
        }

        if(block.hashables.previous != info.head)
        {
            result.code = logos::process_result::fork;
            return false;
        }

        // TODO
        uint64_t current_epoch = 0;

        // Account is not reserved.
        if(info.reservation.is_zero())
        {
            info.reservation = hash;
            info.reservation_epoch = current_epoch;
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
        }

        if(block.hashables.amount.number() > info.balance)
        {
            result.code = logos::process_result::balance_mismatch;
            return false;
        }
    }

    // account doesn't exist
    else
    {
        // Currently do not accept state blocks
        // with non-existent accounts.
        result.code = logos::process_result::not_implemented;
        return false;

        if(!block.hashables.previous.is_zero())
        {
            return false;
        }
    }

    result.code = logos::process_result::progress;
    return true;
}

bool PersistenceManager::Validate(const logos::state_block & block, uint8_t delegate_id)
{
    logos::process_return ignored_result;
    return Validate(block, ignored_result, delegate_id);
}

void PersistenceManager::StoreBatchMessage(const BatchStateBlock & message, MDB_txn * transaction)
{
    auto hash(_store.batch_block_put(message, transaction));

    StateBlockLocator locator_template {hash, 0};

    for(uint64_t i = 0; i < CONSENSUS_BATCH_SIZE; ++i)
    {
        locator_template.index = i;
        _store.state_block_put(message.blocks[i],
                               locator_template,
                               transaction);
    }
}

void PersistenceManager::ApplyBatchMessage(const BatchStateBlock & message, uint8_t delegate_id, MDB_txn * transaction)
{
    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        ApplyStateMessage(message.blocks[i],
                          message.timestamp,
                          transaction);

        std::lock_guard<std::mutex> lock(_reservation_mutex);
        _reservations.Release(message.blocks[i].hashables.account);
    }

    _store.batch_tip_put(delegate_id, message.Hash(), transaction);
}

// Currently designed only to handle
// send transactions.
void PersistenceManager::ApplyStateMessage(
        const logos::state_block & block,
        uint64_t timestamp,
        MDB_txn * transaction)
{
    if(!UpdateSourceState(block, transaction))
    {
        UpdateDestinationState(block, timestamp, transaction);
    }
}

bool PersistenceManager::UpdateSourceState(const logos::state_block & block, MDB_txn * transaction)
{
    logos::account_info info;
    auto account_error(_store.account_get(block.hashables.account, info));

    if(account_error)
    {
        BOOST_LOG (_log) << "PersistenceManager::ApplyStateMessage - Unable to find account.";
        return true;
    }

    info.block_count++;
    info.balance = info.balance.number() - block.hashables.amount.number();
    info.head = block.hash();
    info.modified = logos::seconds_since_epoch();

    _store.account_put(block.hashables.account, info, transaction);

    return false;
}

void PersistenceManager::UpdateDestinationState(
        const logos::state_block & block,
        uint64_t timestamp,
        MDB_txn * transaction)
{
    logos::account_info info;
    auto account_error(_store.account_get(block.hashables.link, info));

    logos::state_block receive(
            /* Account   */ logos::account(block.hashables.link),
            /* Previous  */ info.receive_head,
            /* Rep       */ 0,
            /* Amount    */ block.hashables.amount,
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

    info.receive_head = hash;
    info.balance = info.balance.number() + block.hashables.amount.number();
    info.modified = logos::seconds_since_epoch();

    _store.account_put(logos::account(block.hashables.link),
                       info, transaction);

    PlaceReceive(receive, transaction);
}

void PersistenceManager::PlaceReceive(
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
            if(!_store.state_block_get(cur.hashables.previous, cur, transaction))
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

    _store.receive_put(hash, receive, transaction);
}
