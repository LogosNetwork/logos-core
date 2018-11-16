/// @file
/// This file contains the declaration of the NodeIdentityManager class, which encapsulates
/// node identity management logic. Currently it holds all delegates ip, accounts, and delegate's index (formerly id)
/// into epoch's voted delegates. It also creates genesis microblocks, epochs, and delegates genesis accounts
///
#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>

uint8_t DelegateIdentityManager::_global_delegate_idx = 0;
logos::account DelegateIdentityManager::_delegate_account = 0;
DelegateIdentityManager::IPs DelegateIdentityManager::_delegates_ip;
bool DelegateIdentityManager::_epoch_transition_enabled = true;

DelegateIdentityManager::DelegateIdentityManager(Store &store,
                                         const Config &config)
    : _store(store)
{
   Init(config);
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE HARD-CODED PUB KEYS!!! TODO
void
DelegateIdentityManager::CreateGenesisBlocks(logos::transaction &transaction)
{
    logos::block_hash epoch_hash(0);
    logos::block_hash microblock_hash(0);
    MDB_txn *tx = transaction;
    // passed in block is overwritten
    auto update = [this, tx](auto msg, auto &block, const BlockHash &next, auto get, auto put) mutable->void{
        if (block.previous != 0)
        {
            if ((_store.*get)(block.previous, block, tx))
            {
                LOG_FATAL(_log) << "update failed to get previous " << msg << " "
                                << block.previous.to_string();
                trace_and_halt();
                return;
            }
            block.next = next;
            (_store.*put)(block, tx);
        }
    };
    for (int e = 0; e <= GENESIS_EPOCH; e++)
    {
        Epoch epoch;
        MicroBlock micro_block;

        micro_block.account = logos::genesis_account;
        micro_block.timestamp = 0;
        micro_block.epoch_number = e;
        micro_block.sequence = 0;
        micro_block.last_micro_block = 0;
        micro_block.previous = microblock_hash;

        microblock_hash = _store.micro_block_put(micro_block, transaction);
        _store.micro_block_tip_put(microblock_hash, transaction);
        update("micro block", micro_block, microblock_hash,
               &BlockStore::micro_block_get, &BlockStore::micro_block_put);

        epoch.epoch_number = e;
        epoch.timestamp = 0;
        epoch.account = logos::genesis_account;
        epoch.micro_block_tip = microblock_hash;
        epoch.previous = epoch_hash;
        for (uint8_t i = 0; i < NUM_DELEGATES; ++i) {
            Delegate delegate = {0, 0, 0};
            if (e != 0 || !_epoch_transition_enabled)
            {
                uint8_t del = i + (e - 1) * 8 * _epoch_transition_enabled;
                char buff[5];
                sprintf(buff, "%02x", del + 1);
                logos::keypair pair(buff);
                delegate = {pair.pub, 0, 100000 + (uint64_t)del * 100};
            }
            epoch.delegates[i] = delegate;
        }

        epoch_hash = _store.epoch_put(epoch, transaction);
        _store.epoch_tip_put(epoch_hash, transaction);
        update("epoch", epoch, epoch_hash,
               &BlockStore::epoch_get, &BlockStore::epoch_put);
    }
}

void
DelegateIdentityManager::Init(const Config &config)
{
    logos::transaction transaction (_store.environment, nullptr, true);

    _epoch_transition_enabled = config.all_delegates.size() == 2 * config.delegates.size();

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
            LOG_FATAL(_log) << "NodeIdentityManager::Init Failed to get epoch: " << epoch_tip.to_string();
            trace_and_halt();
        }

        // if a node starts after epoch transition start but before the last microblock
        // is proposed then the latest epoch block is not created yet and the epoch number
        // has to be increamented by 1
        epoch_number = previous_epoch.epoch_number + 1;
        epoch_number = (StaleEpoch()) ? epoch_number + 1 : epoch_number;
    }

    // TBD: this is done out of order, genesis accounts are created in node::node(), needs to be reconciled
    LoadGenesisAccounts();

    _delegate_account = logos::genesis_delegates[config.delegate_id].key.pub;
    _global_delegate_idx = config.delegate_id;

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
DelegateIdentityManager::CreateGenesisAccounts(logos::transaction &transaction)
{
    // create genesis accounts
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        logos::genesis_delegate delegate{logos::keypair(buff), 0, 100000 + (uint64_t) del * 100};
        logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);

        uint128_t min_fee = 0x21e19e0c9bab2400000_cppui128;
        logos::amount amount(min_fee + (del + 1) * 1000000);
        logos::amount fee(min_fee);
        uint64_t work = 0;

        logos::state_block state(pair.pub,  // account
                                 0,         // previous
                                 pair.pub,  // representative
                                 amount,
                                 fee,       // transaction fee
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
DelegateIdentityManager::LoadGenesisAccounts()
{
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        logos::genesis_delegate delegate{logos::keypair(buff), 0, 100000 + (uint64_t) del * 100};
        logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);
    }
}

void
DelegateIdentityManager::IdentifyDelegates(
    EpochDelegates epoch_delegates,
    uint8_t &delegate_idx)
{
    Accounts delegates;
    IdentifyDelegates(epoch_delegates, delegate_idx, delegates);
}

void
DelegateIdentityManager::IdentifyDelegates(
    EpochDelegates epoch_delegates,
    uint8_t &delegate_idx,
    Accounts & delegates)
{
    delegate_idx = NON_DELEGATE;

    bool stale_epoch = StaleEpoch();
    // requested epoch block is not created yet
    if (stale_epoch && epoch_delegates == EpochDelegates::Next)
    {
        LOG_ERROR(_log) << "NodeIdentityManager::IdentifyDelegates delegates set is requested for next epoch";
        return;
    }

    logos::block_hash epoch_tip;
    if (_store.epoch_tip_get(epoch_tip))
    {
        LOG_FATAL(_log) << "NodeIdentityManager::IdentifyDelegates failed to get epoch tip";
        trace_and_halt();
    }

    Epoch epoch;
    if (_store.epoch_get(epoch_tip, epoch))
    {
        LOG_FATAL(_log) << "NodeIdentityManager::IdentifyDelegates failed to get epoch: "
                        << epoch_tip.to_string();
        trace_and_halt();
    }

    if (!stale_epoch && epoch_delegates == EpochDelegates::Current)
    {
        if (_store.epoch_get(epoch.previous, epoch))
        {
            LOG_FATAL(_log) << "NodeIdentityManager::IdentifyDelegates failed to get current delegate's epoch: "
                            << epoch.previous.to_string();
            trace_and_halt();
        }
    }

    // Is this delegate included in the current/next epoch consensus?
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

bool
DelegateIdentityManager::IdentifyDelegates(
    uint epoch_number,
    uint8_t &delegate_idx,
    Accounts & delegates)
{
    logos::block_hash hash;
    if (_store.epoch_tip_get(hash))
    {
        LOG_FATAL(_log) << "NodeIdentityManager::IdentifyDelegates failed to get epoch tip";
        trace_and_halt();
    }

    auto get = [this](logos::block_hash &hash, Epoch &epoch) {
        if (_store.epoch_get(hash, epoch))
        {
            LOG_FATAL(_log) << "NodeIdentityManager::IdentifyDelegates failed to get epoch: "
                            << hash.to_string();
            trace_and_halt();
        }
        return true;
    };

    Epoch epoch;
    bool found = false;
    for (bool res = get(hash, epoch);
              res && !(found = epoch.epoch_number == epoch_number);
              res = get(hash, epoch))
    {
        hash = epoch.previous;
    }

    if (found)
    {
        // Is this delegate included in the current/next epoch consensus?
        delegate_idx = NON_DELEGATE;
        for (uint8_t del = 0; del < NUM_DELEGATES; ++del) {
            // update delegates for the requested epoch
            delegates[del] = epoch.delegates[del].account;
            if (epoch.delegates[del].account == _delegate_account) {
                delegate_idx = del;
            }
        }
    }

    return found;
}

bool
DelegateIdentityManager::StaleEpoch()
{
    auto now_msec = GetStamp();
    auto rem = Seconds(now_msec % TConvert<Milliseconds>(EPOCH_PROPOSAL_TIME).count());
    return (rem < MICROBLOCK_PROPOSAL_TIME);
}
