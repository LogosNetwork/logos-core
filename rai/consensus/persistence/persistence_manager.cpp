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
    for(uint8_t i = 0; i < message.block_count; ++i)
    {
        ApplyStateMessage(message.blocks[i]);
    }
}

bool PersistenceManager::Validate(const rai::state_block & block, rai::process_return & result)
{
    // Have we seen this block before?
    if(_store.state_block_exists(block))
    {
        result.code = rai::process_result::old;
        return false;
    }

    if(block.hashables.account.is_zero())
    {
        result.code = rai::process_result::opened_burn_account;
        return false;
    }

    auto is_send(false);

    rai::account_info info;
    auto account_error(_store.account_get(block.hashables.account, info));

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
            if(!_store.state_block_exists(block.hashables.previous))
            {
                result.code = rai::process_result::gap_previous;
                return false;
            }
        }

        is_send = block.hashables.balance < info.balance;

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

    if(!is_send)
    {
        // Currently, we only accept send requests
        result.code = rai::process_result::not_implemented;
        return false;

        if(block.hashables.link.is_zero())
        {
            return false;
        }
    }

    result.code = rai::process_result::progress;
    return true;
}

bool PersistenceManager::Validate(const rai::state_block & block)
{
    rai::process_return ignored_result;
    return Validate(block, ignored_result);
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
        rai::state_block open(/* Account  */ rai::account(block.hashables.link),
                              /* Previous */ 0,
                              /* Rep      */ 0,
                              /* Balance  */ quantity,
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
                                /* Balance */ quantity,
                                /* Time    */ rai::seconds_since_epoch(),
                                /* Count   */ 0
                          });
    }

    // Destination account exists already
    else
    {
        info.balance = info.balance.number() + quantity.number();
        info.modified = rai::seconds_since_epoch();
    }
}
