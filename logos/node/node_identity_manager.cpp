/// @file
/// This file contains the declaration of the NodeIdentityManager class, which encapsulates
/// node identity management logic. Currently it holds all delegates ip, accounts, and delegate's index (formerly id)
/// into epoch's voted delegates. It also creates genesis microblocks, epochs, and delegates genesis accounts
///
#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/node/node_identity_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/node/node.hpp>

bool NodeIdentityManager::_run_local = false;
uint8_t NodeIdentityManager::_global_delegate_idx = 0;
logos::account NodeIdentityManager::_delegate_account = 0;
NodeIdentityManager::IPs NodeIdentityManager::_delegates_ip;

NodeIdentityManager::NodeIdentityManager(Store &store,
                                         const Config &config)
    : _store(store)
{
   Init(config);
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE HARD-CODED PUB KEYS!!! TODO
void
NodeIdentityManager::CreateGenesisBlocks(logos::transaction &transaction)
{
    logos::block_hash epoch_hash(0);
    logos::block_hash microblock_hash(0);
    for (int e = 0; e <= GENESIS_EPOCH; e++)
    {
        Epoch epoch;
        MicroBlock micro_block;

        micro_block.account = logos::genesis_account;
        micro_block.timestamp = 0;
        micro_block.epoch_number = e;
        micro_block.micro_block_number = 0;
        micro_block.last_micro_block = 0;
        micro_block.previous = microblock_hash;

        microblock_hash = _store.micro_block_put(micro_block, transaction);
        _store.micro_block_tip_put(microblock_hash, transaction);

        epoch.epoch_number = e;
        epoch.timestamp = 0;
        epoch.account = logos::genesis_account;
        epoch.micro_block_tip = microblock_hash;
        epoch.previous = epoch_hash;
        for (uint8_t i = 0; i < NUM_DELEGATES; ++i) {
            Delegate delegate = {0, 0, 0};
            if (e != 0)
            {
                uint8_t del = i + (e - 1) * 8;
                char buff[5];
                sprintf(buff, "%02x", del);
                logos::keypair pair(buff);
                delegate = {pair.pub, 0, 100000 + (uint64_t)del * 100};
            }
            epoch.delegates[i] = delegate;
        }

        epoch_hash = _store.epoch_put(epoch, transaction);
        _store.epoch_tip_put(epoch_hash, transaction);
    }
}

void
NodeIdentityManager::Init(const Config &config)
{
    logos::transaction transaction (_store.environment, nullptr, true);

    logos::block_hash epoch_tip;
    uint16_t epoch_number = 0;
    if (_store.epoch_tip_get(epoch_tip))
    {
        CreateGenesisBlocks(transaction);
        epoch_number = GENESIS_EPOCH + 1;
    }
    else
    {
        Epoch previous_epoch;
        if (_store.epoch_get(epoch_tip, previous_epoch))
        {
            BOOST_LOG(_log) << "NodeIdentityManager::Init Failed to get epoch: " << epoch_tip.to_string();
            return;
        }

        epoch_number = previous_epoch.epoch_number + 1;
    }

    // TBD: this is done out of order, genesis accounts are created in node::node(), needs to be reconciled
    LoadGenesisAccounts();

    _delegate_account = logos::genesis_delegates[config.delegate_id].key.pub;
    _global_delegate_idx = config.delegate_id;
    _run_local = config.run_local;

    ConsensusContainer::SetCurEpochNumber(epoch_number);

    // get all ip's
    for (uint8_t del = 0; del < 2 * NUM_DELEGATES && del < config.all_delegates.size(); ++del)
    {
        auto account = logos::genesis_delegates[del].key.pub;
        auto ip = config.all_delegates[del].ip;
        _delegates_ip[account] = ip;
    }
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE PRIVATE KEYS ARE 0-63!!! TBD
void
NodeIdentityManager::CreateGenesisAccounts(logos::transaction &transaction)
{
    // create genesis accounts
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del);
        logos::genesis_delegate delegate{logos::keypair(buff), 0, 100000 + (uint64_t) del * 100};
        logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);

        logos::amount amount(100000 + del * 100);
        uint64_t work = 0;

        logos::state_block state(pair.pub,  // account
                                 0,         // previous
                                 pair.pub,  // representative
                                 amount,
                                 pair.pub,  // link
                                 pair.prv,
                                 pair.pub,
                                 work);

        _store.receive_put(state.hash(),
                           state,
                           transaction);

        _store.account_put(pair.pub,
                           {
                               /* Head    */ 0,
                               /* Previous*/ 0,
                               /* Rep     */ 0,
                               /* Open    */ state.hash(),
                               /* Amount  */ amount,
                               /* Time    */ logos::seconds_since_epoch(),
                               /* Count   */ 0
                           },
                           transaction);
    }
}

void
NodeIdentityManager::LoadGenesisAccounts()
{
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del);
        logos::genesis_delegate delegate{logos::keypair(buff), 0, 100000 + (uint64_t) del * 100};
        logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);
    }
}

void
NodeIdentityManager::IdentifyDelegates(
    EpochDelegates epoch_delegates,
    uint8_t &delegate_idx)
{
    Accounts delegates;
    IdentifyDelegates(epoch_delegates, delegate_idx, delegates);
}

void
NodeIdentityManager::IdentifyDelegates(
    EpochDelegates epoch_delegates,
    uint8_t &delegate_idx,
    Accounts & delegates)
{
    logos::block_hash epoch_tip;
    if (_store.epoch_tip_get(epoch_tip))
    {
        BOOST_LOG(_log) << "NodeIdentityManager::IdentifyDelegates failed to get epoch tip";
        return;
    }

    Epoch epoch;
    if (_store.epoch_get(epoch_tip, epoch))
    {
        BOOST_LOG(_log) << "NodeIdentityManager::IdentifyDelegates failed to get epoch: " <<
            epoch_tip.to_string();
        return;
    }

    if (epoch_delegates == EpochDelegates::Current && _store.epoch_get(epoch.previous, epoch))
    {
        BOOST_LOG(_log) << "NodeIdentityManager::IdentifyDelegates failed to get current delegate's epoch: " <<
            epoch.previous.to_string();
        return;
    }

    // Is this delegate included in the current/next epoch consensus?
    delegate_idx = NON_DELEGATE;
    for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
    {
        // update delegates for the requested epoch
        delegates[del] = epoch.delegates[del].account;
        if (epoch.delegates[del].account == _delegate_account)
        {
            delegate_idx = del;
        }
    }
}