#include <rai/consensus/persistence/persistence_manager.hpp>
#include <rai/consensus/persistence/state_block_locator.hpp>
#include <rai/common.hpp>
#include <rai/node/common.hpp>

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

void PersistenceManager::ApplyBatchMessage(const BatchStateBlock & message)
{
    for(uint8_t i = 0; i < CONSENSUS_BATCH_SIZE; ++i)
    {
        ApplyStateMessage(message.blocks[i]);
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
        // Currently do not accept state blocks
        // with non-existent accounts.
        return false;

        if(!block.hashables.previous.is_zero())
        {
            return false;
        }
    }

    if(!is_send)
    {
        // Currently, we only accept send requests
        return false;

        if(block.hashables.link.is_zero())
        {
            return false;
        }
    }

    return true;
}

// Currently designed only to handle
// send transactions.
void PersistenceManager::ApplyStateMessage(const rai::state_block & block)
{
    rai::amount quantity;

    if(!UpdateSourceState(block, quantity))
    {
        UpdateDestinationState(block, quantity);
    }
}

bool PersistenceManager::UpdateSourceState(const rai::state_block & block, rai::amount & quantity)
{
    rai::account_info info;
    auto account_error(_store.account_get(block.hashables.account, info));

    if(account_error)
    {
        BOOST_LOG (_log) << "PersistenceManager::ApplyStateMessage - Unable to find account.";
        return true;
    }

    quantity = info.balance.number() - block.hashables.balance.number();

    info.block_count++;
    info.balance = block.hashables.balance;
    info.head = block.hash();
    info.modified = rai::seconds_since_epoch();

    _store.account_put(block.hashables.account, info);

    return false;
}

void PersistenceManager::UpdateDestinationState(const rai::state_block & block, rai::amount quantity)
{
    rai::account_info info;
    auto account_error(_store.account_get(block.hashables.link, info));

    // Destination account doesn't exist yet
    if(account_error)
    {
       _store.account_put(rai::account(block.hashables.link),
                          {
                                /* No head */ 0,
                                /* No Rep  */ 0,
                                /* No Open */ 0,
                                quantity,
                                rai::seconds_since_epoch(),
                                0
                          });
    }

    // Destination account exists already
    else
    {
        info.balance = info.balance.number() + quantity.number();
        info.modified = rai::seconds_since_epoch();
    }
}

