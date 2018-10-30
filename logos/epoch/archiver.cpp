///
/// @file
/// This file contains definition of the Archiver class - container for epoch/microblock handling related classes
///

#include <logos/epoch/archiver.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/lib/epoch_time_util.hpp>

Archiver::Archiver(logos::alarm & alarm,
                   BlockStore & store,
                   IRecallHandler & recall_handler)
    : _event_proposer(alarm, IsFirstEpoch(store))
    , _micro_block_handler(store, recall_handler)
    , _voting_manager(store)
    , _epoch_handler(store, _voting_manager)
    , _recall_handler(recall_handler)
    , _store(store)
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

    auto transition_cb = [&consensus](){
        consensus.EpochTransitionEventsStart();
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

    return epoch.epoch_number == GENESIS_EPOCH;
}
