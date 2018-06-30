#include <rai/consensus/persistence/persistence_manager.hpp>
#include <rai/consensus/persistence/state_block_locator.hpp>

PersistenceManager::PersistenceManager(Store & store)
    : _store(store)
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

bool PersistenceManager::Validate(const rai::state_block & block)
{
    // Have we seen this block before?
    if(_store.state_block_exists(block))
    {
        return false;
    }

    if(block.hashables.account.is_zero())
    {
        return false;
    }

    auto is_send(false);

    rai::account_info info;
    auto account_error(_store.account_get(block.hashables.account, info));

    // account exists
    if(!account_error)
    {
        // no previous block set
        if(block.hashables.previous.is_zero())
        {
            return false;
        }

        if(!_store.state_block_exists(block.hashables.previous))
        {
            return false;
        }

        is_send = block.hashables.balance < info.balance;

        if(block.hashables.previous != info.head)
        {
            return false;
        }
    }

    // account doesn't exist
    else
    {
        if(!block.hashables.previous.is_zero())
        {
            return false;
        }
    }

    if(!is_send)
    {
        if(block.hashables.link.is_zero())
        {
            return false;
        }
    }

    return true;
}
