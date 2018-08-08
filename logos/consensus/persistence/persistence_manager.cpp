#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/persistence/state_block_locator.hpp>
#include <logos/common.hpp>

PersistenceManager::PersistenceManager(Store & store,
                                       Log & log)
    : _store(store)
    , _log(log)
{}

void PersistenceManager::ApplyUpdates(const BatchStateBlock & message, uint8_t delegate_id)
{
    logos::transaction transaction(_store.environment, nullptr, true);

    StoreBatchMessage(message, transaction);
    ApplyBatchMessage(message, delegate_id, transaction);

    ClearCache(delegate_id);
}

bool PersistenceManager::Validate(const logos::state_block & block, logos::process_return & result, uint8_t delegate_id)
{
    auto hash = block.hash();
    auto & store = GetStore(delegate_id);

    // Have we seen this block before?
    if(store.StateBlockExists(hash))
    {
        result.code = logos::process_result::old;
        return false;
    }

    if(block.hashables.account.is_zero())
    {
        result.code = logos::process_result::opened_burn_account;
        return false;
    }

    logos::account_info info;
    auto account_error(store.GetAccount(block.hashables.account, info));

    // account exists
    if(!account_error)
    {
        // no previous block set
        if(block.hashables.previous.is_zero() && info.block_count)
        {
            result.code = logos::process_result::fork;
            return false;
        }

        // This account has issued at least one send transaction.
        if(info.block_count)
        {
            if(!store.StateBlockExists(block.hashables.previous))
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

    // Cache this block so that subsequent
    // send requests may refer to it before
    // it has been confirmed by validation.
    store.pending_blocks.insert(hash);


    info.block_count++;
    info.head = block.hash();

    // Also cache pending account changes
    store.pending_account_changes[block.hashables.account] = info;

    result.code = logos::process_result::progress;
    return true;
}

bool PersistenceManager::Validate(const logos::state_block & block, uint8_t delegate_id)
{
    logos::process_return ignored_result;
    return Validate(block, ignored_result, delegate_id);
}

void PersistenceManager::ClearCache(uint8_t delegate_id)
{
    GetStore(delegate_id).ClearCache();
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
        ApplyStateMessage(message.blocks[i], transaction);
    }

    _store.batch_tip_put(delegate_id, message.Hash(), transaction);
}

// Currently designed only to handle
// send transactions.
void PersistenceManager::ApplyStateMessage(const logos::state_block & block, MDB_txn * transaction)
{
    if(!UpdateSourceState(block, transaction))
    {
        UpdateDestinationState(block, transaction);
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

void PersistenceManager::UpdateDestinationState(const logos::state_block & block, MDB_txn * transaction)
{
    logos::account_info info;
    auto account_error(_store.account_get(block.hashables.link, info));

    // Destination account doesn't exist yet
    if(account_error)
    {
        logos::state_block open(/* Account  */ logos::account(block.hashables.link),
                              /* Previous */ 0,
                              /* Rep      */ 0,
                              /* Amount   */ block.hashables.amount,
                              /* Link     */ block.hash(),
                              /* Priv Key */ logos::raw_key(),
                              /* Pub Key  */ logos::public_key(),
                              /* Work     */ 0);

        auto hash(open.hash());

        _store.receive_put(hash, open, transaction);
        _store.account_put(logos::account(block.hashables.link),
                           {
                               /* Head    */ 0,
                               /* Rep     */ hash,
                               /* Open    */ hash,
                               /* Amount  */ block.hashables.amount,
                               /* Time    */ logos::seconds_since_epoch(),
                               /* Count   */ 0
                           },
                           transaction);
    }

    // Destination account exists already
    else
    {
        info.balance = info.balance.number() + block.hashables.amount.number();
        info.modified = logos::seconds_since_epoch();

        _store.account_put(logos::account(block.hashables.link), info,
                           transaction);
    }
}

PersistenceManager::DynamicStorage & PersistenceManager::GetStore(uint8_t delegate_id)
{
    std::lock_guard<std::mutex> lock(_dynamic_storage_mutex);

    if(_dynamic_storage.find(delegate_id) == _dynamic_storage.end())
    {
        _dynamic_storage.insert({delegate_id, DynamicStorage(_store)});
    }

    return _dynamic_storage.find(delegate_id)->second;
}
