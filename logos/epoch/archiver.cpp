///
/// @file
/// This file contains definition of the Archiver class - container for epoch/microblock handling related classes
///

#include <logos/epoch/archiver.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/lib/epoch_time_util.hpp>

Archiver::Archiver(logos::alarm & alarm,
                   BlockStore & store,
                   IRecallHandler & recall_handler,
                   uint8_t delegate_id)
    : _event_proposer(alarm, IsFirstEpoch(store))
    , _micro_block_handler(store, delegate_id, recall_handler)
    , _voting_manager(store)
    , _epoch_handler(store, delegate_id, _voting_manager)
    , _recall_handler(recall_handler)
    , _store(store)
    , _delegate_id(delegate_id)
    {}

void
Archiver::Start(InternalConsensus &consensus)
{
    auto micro_cb = [this, &consensus](){
        EpochTimeUtil util;
        auto micro_block = std::make_shared<MicroBlock>();
       if (false == _micro_block_handler.Build(*micro_block, util.IsEpochTime()))
       {
           BOOST_LOG(_log) << "Archiver::Start failed to build micro block";
           return;
       }

       consensus.OnSendRequest(micro_block);
    };

    auto epoch_cb = [this, &consensus]()->void
    {
        auto epoch = std::make_shared<Epoch>();
        if (false == _epoch_handler.Build(*epoch))
        {
            BOOST_LOG(_log) << "Archiver::Start failed to build epoch block";
            return;
        }

        consensus.OnSendRequest(epoch);
    };

    auto transition_cb = [this](){
        BOOST_LOG(_log) << "Archiver::Start EPOCH TRANSITION IS NOT IMPLEMENTED";
    };

    _event_proposer.Start(micro_cb, transition_cb, epoch_cb);
}

void
Archiver::Test_ProposeMicroBlock(InternalConsensus &consensus, bool last_microblock)
{
    _event_proposer.ProposeMicroBlockOnce([this, &consensus, last_microblock]()->void {
        auto micro_block = std::make_shared<MicroBlock>();
        if (false == _micro_block_handler.Build(*micro_block, last_microblock))
        {
            BOOST_LOG(_log) << "Archiver::Test_ProposeMicroBlock faile to build micro block";
            return;
        }
        consensus.OnSendRequest(micro_block);
    });
}

void
Archiver::CreateGenesisBlocks(logos::transaction &transaction)
{
    logos::block_hash epoch_hash(0);
    logos::block_hash microblock_hash(0);
    for (int e = 0; e <= GENESIS_EPOCH; e++)
    {
        Epoch epoch;
        MicroBlock micro_block;

        micro_block._delegate = logos::genesis_account;
        micro_block.timestamp = 0;
        micro_block._epoch_number = e;
        micro_block._micro_block_number = 0;
        micro_block._last_micro_block = 0;
        micro_block.previous = microblock_hash;

        microblock_hash = _micro_block_handler.ApplyUpdates(micro_block, transaction);

        epoch._epoch_number = e;
        epoch.timestamp = 0;
        epoch._account = logos::genesis_account;
        epoch._micro_block_tip = microblock_hash;
        epoch.previous = epoch_hash;
        for (uint8_t i = 0; i < NUM_DELEGATES; ++i) {
            Delegate delegate = {0, 0, 0};
            if (0 != i)
            {
                uint64_t del = i + (e - 1) * 8;
                delegate = {logos::genesis_delegates[del].key.pub, 0, 100000 + del * 100};
            }
            epoch._delegates[i] = delegate;
        }

        epoch_hash = _epoch_handler.ApplyUpdates(epoch, transaction);
    }
}

bool
Archiver::IsFirstEpoch(BlockStore &store)
{
    BlockHash hash;
    Epoch epoch;

    if (store.epoch_tip_get(hash))
    {
        Log log;
        BOOST_LOG(log) <<
            "Archiver::IsFirstEpoch failed to get epoch tip. Genesis blocks are being generated.";
        return true;
    }

    if (store.epoch_get(hash, epoch))
    {
        BOOST_LOG(_log) << "Archiver::IsFirstEpoch failed to get epoch: " <<
            hash.to_string();
        return false;
    }

    return epoch._epoch_number == GENESIS_EPOCH;
}