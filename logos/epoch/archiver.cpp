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
    , _mb_message_handler(MicroBlockMessageHandler::GetMessageHandler())
    , _recall_handler(recall_handler)
    , _store(store)
{
    BlockHash mb_tip;
    // fetch latest microblock (this requires DelegateIdentityManager be initialized earlier inside `node`)
    if (_store.micro_block_tip_get(mb_tip))
    {
        LOG_FATAL(_log) << "Archiver::Archiver - Failed to get microblock tip";
        trace_and_halt();
    }
    ApprovedMB mb;
    if (_store.micro_block_get(mb_tip, mb))
    {
        LOG_FATAL(_log) << "Archiver::Archiver - Failed to get microblock";
        trace_and_halt();
    }
    _mb_seq = mb.sequence;
    _eb_num = mb.epoch_number;
}

void
Archiver::Start(InternalConsensus &consensus)
{
    auto micro_cb = [this, &consensus](){
        EpochTimeUtil util;
        auto micro_block = std::make_shared<DelegateMessage<ConsensusType::MicroBlock>>();
        bool is_epoch_time = util.IsEpochTime();
        bool last_microblock = !_recall_handler.IsRecall() && is_epoch_time && !_first_epoch;

        bool one_mb_past = util.IsOneMBPastEpochTime();

        // check if latest in db / queue is the same as our own in-memory counter
        // get DB block first
        ApprovedMB mb;
        {
            BlockHash mb_tip;
            // use write transaction to ensure sequencing
            logos::transaction tx(_store.environment, nullptr, true);
            if (_store.micro_block_tip_get(mb_tip, tx))
            {
                LOG_FATAL(_log) << "Archiver::Archiver - Failed to get microblock tip";
                trace_and_halt();
            }
            if (_store.micro_block_get(mb_tip, mb, tx))
            {
                LOG_FATAL(_log) << "Archiver::Archiver - Failed to get microblock";
                trace_and_halt();
            }
        }

        // check queue content
        uint32_t latest_mb_seq, latest_eb_num;
        _mb_message_handler.GetQueuedSequence(latest_mb_seq, latest_eb_num);
        if (!latest_mb_seq && !latest_eb_num)  // both being 0 indicates queue is empty
        {
            latest_mb_seq = mb.sequence;
            latest_eb_num = mb.epoch_number;
        }
        else  // queued number must be greater than database-stored number
        {
            assert (latest_eb_num > mb.epoch_number || (latest_eb_num == mb.epoch_number && latest_mb_seq > mb.sequence));
        }

        // TODO: Archiver's internal counter should really be directly updated by post commit
        if (_mb_seq != latest_mb_seq || _eb_num != latest_eb_num)
        {
            if (_eb_num > latest_eb_num || (_eb_num == latest_eb_num && _mb_seq > latest_mb_seq))
            {
                // we are exactly one ahead of db, meaning that the current consensus session isn't done yet, do nothing
                if ((_eb_num == latest_eb_num + 1 && !_mb_seq) ||
                    (_eb_num == latest_eb_num && _mb_seq == latest_mb_seq + 1))
                {
                    return;
                }
                // we somehow ended up ahead more than one ahead, should not happen
                else
                {
                    LOG_FATAL(_log) << "Archiver::Start - unexpected scenario, internal counter out of sync";
                    trace_and_halt();
                }
            }
            // we are behind in time, catch up and don't build
            // TODO: sync clock?
            else if (_eb_num < latest_eb_num || (_eb_num == latest_eb_num && _mb_seq < latest_mb_seq))
            {
                _eb_num = latest_eb_num;
                _mb_seq = latest_mb_seq;
                return;
            }
        }

        // if internal counter match db record, then we can go ahead and build the next MB

        if (!_micro_block_handler.Build(*micro_block, last_microblock))
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

        _mb_seq = micro_block->sequence;
        _eb_num = micro_block->epoch_number;

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
Archiver::OnApplyUpdates(const ApprovedMB &block)
{
    if (block.last_micro_block) {
        uint32_t epoch_number_stored;
        {
            // use write transaction to ensure sequencing
            logos::transaction tx(_store.environment, nullptr, true);
            epoch_number_stored = _store.epoch_number_stored();
        }
        // avoid duplicate proposals
        if (epoch_number_stored + 1 != block.epoch_number)
        {
            LOG_WARN(_log) << "Archiver::OnApplyUpdates - skipping duplicate epoch block construction.";
            return;
        }
        _event_proposer.ProposeEpoch();
    }
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