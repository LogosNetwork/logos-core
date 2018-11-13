#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/persistence/state_block_locator.hpp>
#include <logos/common.hpp>

constexpr uint128_t PersistenceManager::MIN_TRANSACTION_FEE;

PersistenceManager::PersistenceManager(Store & store)
    : _reservations(store)
    , _store(store)
{}

// Ask DEVON
// Can we pass in a transaction, and then if we fail to validate, can we mdb_txn_abort() it ?
// And then, bootstrap from another peer ?
void PersistenceManager::ApplyUpdates(const BatchStateBlock & message, uint8_t delegate_id)
{
    logos::transaction transaction(_store.environment, nullptr, true);

    StoreBatchMessage(message, transaction, delegate_id);
    ApplyBatchMessage(message, transaction);
}

bool PersistenceManager::Validate(const logos::state_block & block,
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
                std::cout << "block.hashables.previous: " << block.hashables.previous.to_string() << " info.head: " << info.head.to_string() << " hash: " << hash.to_string() << std::endl;
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

bool PersistenceManager::Validate(const logos::state_block & block)
{
    logos::process_return ignored_result;
    return Validate(block, ignored_result);
}

void PersistenceManager::StoreBatchMessage(const BatchStateBlock & message,
                                           MDB_txn * transaction,
                                           uint8_t delegate_id)
{
    BatchStateBlock prev;

    if(_store.batch_block_get(message.previous, prev, transaction))
    {
        // TODO: bootstrap here.
        //
        if(!message.previous.is_zero())
        {
            std::cout << "message.hash: " << message.Hash().to_string() << " previous: " << message.previous.to_string() << std::endl;
            LOG_FATAL(_log) << "PersistenceManager::StoreBatchMessage - "
                            << "Failed to find previous: "
                            << message.previous.to_string();

            std::exit(EXIT_FAILURE);
        }
    }

    auto hash(_store.batch_block_put(message, transaction));

    prev.next = hash;
    _store.batch_block_put(prev, transaction);

    StateBlockLocator locator_template {hash, 0};

    for(uint64_t i = 0; i < CONSENSUS_BATCH_SIZE; ++i)
    {
        locator_template.index = i;
        _store.state_block_put(message.blocks[i],
                               locator_template,
                               transaction);
    }

    _store.batch_tip_put(delegate_id, message.Hash(), transaction);
}

void PersistenceManager::ApplyBatchMessage(const BatchStateBlock & message, MDB_txn * transaction)
{
    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        ApplyStateMessage(message.blocks[i],
                          message.timestamp,
                          transaction);

        std::lock_guard<std::mutex> lock(_reservation_mutex);
        _reservations.Release(message.blocks[i].hashables.account);
    }
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

    _store.account_put(block.hashables.account, info, transaction);

    return false;
}

void PersistenceManager::UpdateDestinationState(
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

    _store.receive_put(hash, receive, transaction);
}
