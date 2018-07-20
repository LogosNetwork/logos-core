#include <rai/consensus/persistence/persistence_manager.hpp>
#include <rai/consensus/persistence/state_block_locator.hpp>
#include <rai/common.hpp>

PersistenceManager::PersistenceManager(Store & store,
                                       Log & log)
    : _store(store)
    , _log(log)
{}

void PersistenceManager::StoreBatchMessage(const BatchStateBlock & message)
{
    auto hash(_store.batch_block_put(message));

    StateBlockLocator locator_template {hash, 0};

    for(uint8_t i = 0; i < CONSENSUS_BATCH_SIZE; ++i)
    {
        locator_template.index = i;
        _store.state_block_put(message.blocks[i], locator_template);
    }
}

void PersistenceManager::ApplyBatchMessage(const BatchStateBlock & message, uint8_t delegate_id)
{
    for(uint8_t i = 0; i < message.block_count; ++i)
    {
        ApplyStateMessage(message.blocks[i]);
    }

    // TODO: Reuse precomputed hash
    //
    _store.batch_tip_put(delegate_id, message.Hash());
}

bool PersistenceManager::Validate(const rai::state_block & block, rai::process_return & result, uint8_t delegate_id)
{
    auto hash = block.hash();
    auto store = GetStore(delegate_id);

    // Have we seen this block before?
    if(store.StateBlockExists(hash))
    {
        result.code = rai::process_result::old;
        return false;
    }

    if(block.hashables.account.is_zero())
    {
        result.code = rai::process_result::opened_burn_account;
        return false;
    }

    rai::account_info info;
    auto account_error(store.GetAccount(block.hashables.account, info));

    // account exists
    if(!account_error)
    {
        // no previous block set
        if(block.hashables.previous.is_zero() && info.block_count)
        {
            result.code = rai::process_result::fork;
            return false;
        }

        // This account has issued at least one send transaction.
        if(info.block_count)
        {
            if(!store.StateBlockExists(block.hashables.previous))
            {
                result.code = rai::process_result::gap_previous;
                return false;
            }
        }

        if(block.hashables.previous != info.head)
        {
            result.code = rai::process_result::fork;
            return false;
        }
    }

    // account doesn't exist
    else
    {
        // Currently do not accept state blocks
        // with non-existent accounts.
        result.code = rai::process_result::not_implemented;
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

    result.code = rai::process_result::progress;
    return true;
}

bool PersistenceManager::Validate(const rai::state_block & block, uint8_t delegate_id)
{
    rai::process_return ignored_result;
    return Validate(block, ignored_result, delegate_id);
}

void PersistenceManager::ClearCache(uint8_t delegate_id)
{
    GetStore(delegate_id).ClearCache();
}

// Currently designed only to handle
// send transactions.
void PersistenceManager::ApplyStateMessage(const rai::state_block & block)
{
    if(!UpdateSourceState(block))
    {
        UpdateDestinationState(block);
    }
}

bool PersistenceManager::UpdateSourceState(const rai::state_block & block)
{
    rai::account_info info;
    auto account_error(_store.account_get(block.hashables.account, info));

    if(account_error)
    {
        BOOST_LOG (_log) << "PersistenceManager::ApplyStateMessage - Unable to find account.";
        return true;
    }

    info.block_count++;
    info.balance = info.balance.number() - block.hashables.amount.number();
    info.head = block.hash();
    info.modified = rai::seconds_since_epoch();

    _store.account_put(block.hashables.account, info);

    return false;
}

void PersistenceManager::UpdateDestinationState(const rai::state_block & block)
{
    rai::account_info info;
    auto account_error(_store.account_get(block.hashables.link, info));

    // Destination account doesn't exist yet
    if(account_error)
    {
        rai::state_block open(/* Account  */ rai::account(block.hashables.link),
                              /* Previous */ 0,
                              /* Rep      */ 0,
                              /* Amount   */ block.hashables.amount,
                              /* Link     */ block.hash(),
                              /* Priv Key */ rai::raw_key(),
                              /* Pub Key  */ rai::public_key(),
                              /* Work     */ 0);

        auto hash(open.hash());

        _store.receive_put(hash, open);
        _store.account_put(rai::account(block.hashables.link),
                           {
                               /* Head    */ 0,
                               /* Rep     */ hash,
                               /* Open    */ hash,
                               /* Amount  */ block.hashables.amount,
                               /* Time    */ rai::seconds_since_epoch(),
                               /* Count   */ 0
                           });
    }

    // Destination account exists already
    else
    {
        info.balance = info.balance.number() + block.hashables.amount.number();
        info.modified = rai::seconds_since_epoch();
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