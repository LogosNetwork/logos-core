///
/// @file
/// This file contains definition of the Archiver class - container for epoch/microblock handling related classes
///

#include <logos/consensus/consensus_container.hpp>
#include <logos/epoch/recall_handler.hpp>
#include <logos/lib/epoch_time_util.hpp>
#include <logos/epoch/archiver.hpp>

Archiver::Archiver(logos::alarm & alarm,
                   BlockStore & store,
                   IRecallHandler & recall_handler)
    : _first_epoch(store.is_first_epoch())
    , _event_proposer(alarm, recall_handler, _first_epoch, IsFirstMicroBlock(store))
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
        auto micro_block = std::make_shared<DelegateMessage<ConsensusType::MicroBlock>>();
        bool is_epoch_time = util.IsEpochTime();
        bool last_microblock = !_recall_handler.IsRecall() && is_epoch_time && !_first_epoch;

        bool one_mb_past = util.IsOneMBPastEpochTime();

        if (false == _micro_block_handler.Build(*micro_block, last_microblock))
        {
            LOG_ERROR(_log) << "Archiver::Start failed to build micro block";
            return;
        }

        if (is_epoch_time
        || (_first_epoch &&
            !micro_block->sequence &&
            micro_block->epoch_number == GENESIS_EPOCH + 1 &&
            one_mb_past))
        {
            _first_epoch = false;
        }


        consensus.OnDelegateMessage(micro_block);
    };

    auto epoch_cb = [this, &consensus]()->void
    {
        auto epoch = std::make_shared<DelegateMessage<ConsensusType::Epoch>>();
        if (false == _epoch_handler.Build(*epoch))
        {
            LOG_ERROR(_log) << "Archiver::Start failed to build epoch block";
            return;
        }

        consensus.OnDelegateMessage(epoch);
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
        auto micro_block = std::make_shared<DelegateMessage<ConsensusType::MicroBlock>>();
        if (false == _micro_block_handler.Build(*micro_block, last_microblock))
        {
            LOG_ERROR(_log) << "Archiver::Test_ProposeMicroBlock failed to build micro block";
            return;
        }
        consensus.OnDelegateMessage(micro_block);
    });
}

bool
Archiver::IsFirstMicroBlock(BlockStore &store)
{
    BlockHash hash;
    ApprovedMB microblock;

    if (store.micro_block_tip_get(hash))
    {
        Log log;
        LOG_ERROR(log) << "Archiver::IsFirstMicroBlock failed to get microblock tip. Genesis blocks are being generated.";
        return true;
    }

    if (store.micro_block_get(hash, microblock))
    {
        LOG_ERROR(_log) << "Archiver::IsFirstMicroBlock failed to get microblock: "
                        << hash.to_string();
        return false;
    }

    return microblock.epoch_number == GENESIS_EPOCH;
}

bool
Archiver::IsRecall()
{
    return _recall_handler.IsRecall();
}