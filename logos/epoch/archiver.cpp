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
                   EventProposer & event_proposer,
                   IRecallHandler & recall_handler,
                   logos::IBlockCache & block_cache)
    : _event_proposer(event_proposer)
    , _micro_block_handler(store, recall_handler)
    , _voting_manager(store)
    , _epoch_handler(store, _voting_manager)
    , _mb_message_handler(MicroBlockMessageHandler::GetMessageHandler())
    , _recall_handler(recall_handler)
    , _store(store)
    , _block_cache(block_cache)
{}

void
Archiver::Start(InternalConsensus &consensus)
{
    // TODO: make sure we are done bootstrapping by this call

    Tip mb_tip;
    // fetch latest microblock (this requires DelegateIdentityManager be initialized earlier inside `node`)
    if (_store.micro_block_tip_get(mb_tip))
    {
        LOG_FATAL(_log) << "Archiver::Archiver - Failed to get microblock tip";
        trace_and_halt();
    }
    ApprovedMB mb;
    if (_store.micro_block_get(mb_tip.digest, mb))
    {
        LOG_FATAL(_log) << "Archiver::Archiver - Failed to get microblock";
        trace_and_halt();
    }
    // Initialize internal counter
    _counter = std::make_pair(mb.epoch_number, mb.sequence);

    auto micro_cb = [this, &consensus](){
        ArchiveMB(consensus);
    };

    auto epoch_cb = [this, &consensus]()->void
    {
        auto epoch = std::make_shared<DelegateMessage<ConsensusType::Epoch>>();
        if (! _epoch_handler.Build(*epoch))
        {
            LOG_ERROR(_log) << "Archiver::Start failed to build epoch block";
            return;
        }

        consensus.OnDelegateMessage(epoch);
    };

    _event_proposer.StartArchival(micro_cb, epoch_cb, _store.is_first_microblock());
}

void
Archiver::Stop()
{
    _event_proposer.StopArchival();
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
    _event_proposer.ProposeMicroBlockOnce([this, &consensus, &last_microblock]()->void {
        auto micro_block = std::make_shared<DelegateMessage<ConsensusType::MicroBlock>>();
        if (! _micro_block_handler.Build(*micro_block))
        {
            LOG_ERROR(_log) << "Archiver::Test_ProposeMicroBlock failed to build micro block";
            return;
        }
        micro_block->last_micro_block = last_microblock;
        consensus.OnDelegateMessage(micro_block);
    });
}

void
Archiver::ArchiveMB(InternalConsensus & consensus)
{
    auto micro_block = std::make_shared<DelegateMessage<ConsensusType::MicroBlock>>();

    {
        // use write transaction to ensure sequencing:
        // if MB backup writes first, then we can reliably get latest MB sequence from DB or MessageHandler Queue
        // if we get tx handle first, then the latest MB sequence must still be in MH queue
        // (since backup DB write takes place before queue clear)
        logos::transaction tx(_store.environment, nullptr, true);
        if (ShouldSkipMBBuild() || !_micro_block_handler.Build(*micro_block)) return;

        _counter.first = micro_block->epoch_number;
        _counter.second = micro_block->sequence;

        // TODO: this is a very hacky fix. The proper solution would be to only trigger
        //  clearing from MessageHandler Queue **after** MicroBlockPersistenceManager ApplyUpdates finishes execution.
        //  Currently the queue clearing is triggered prematurely in BackupDelegate's on PostCommit message logic.
        auto hash = micro_block->Hash();
        if (_mb_message_handler.Contains(hash) || _block_cache.IsBlockCachedOrQueued(hash))
        {
            LOG_DEBUG(_log) << "Archiver::ArchiveMB - MB with hash " << hash.to_string()
                            << " is already in message handler queue, block cache, or write queue. Skipping.";
        }
    }

    consensus.OnDelegateMessage(micro_block);
}

bool
Archiver::ShouldSkipMBBuild()
{
    // Before potentially proposing a new MB, we need to check for 2 conditions:
    // 1) Is there a local clock lag?
    //     The latest MB seq built is max(latest MessageHandler Queue MB seq, DB MB tip seq) if queue is non-empty,
    //     or simply DM MB tip seq if queue is empty.
    //     We check if the internal counter is behind the latest seq.
    //     If it is behind, then we need to catch up by setting the counter to this latest value
    //     (if DB is ahead of latest queue content, something is seriously wrong)
    // 2) Is the current consensus session not complete yet (e.g. b/c OnQuorumFailed)?
    //     We check if the MBQ seq, if one exists, is one ahead of DB MB tip seq.
    //     If 2 ahead, we need to initiate bootstrap sequence
    // Note that conditions 1 and 2 may overlap (MBQ seq would be one ahead of
    // both Archiver internal counter as well as DB tip seq), in which case we also skip the proposal.

    EpochSeq latest, queued, stored;
    bool is_queued;
    uint32_t latest_mb_seq, latest_eb_num, queued_mb_seq, queued_eb_num;
    ApprovedMB mb;
    Tip mb_tip;

    // get DB's latest MB first
    if (_store.micro_block_tip_get(mb_tip))
    {
        LOG_FATAL(_log) << "Archiver::ShouldSkipMBBuild - Failed to get microblock tip";
        trace_and_halt();
    }
    if (_store.micro_block_get(mb_tip.digest, mb))
    {
        LOG_FATAL(_log) << "Archiver::ShouldSkipMBBuild - Failed to get microblock";
        trace_and_halt();
    }
    stored = std::make_pair(mb.epoch_number, mb.sequence);

    // check queue content
    is_queued = _mb_message_handler.GetQueuedSequence(queued);
    LOG_DEBUG(_log) << "Archiver::ShouldSkipMBBuild - queued MB epoch: " << queued.first
                    << "; sequence: " << queued.second;
    if (!is_queued)  // queue is empty
    {
        latest = stored;
    }
    else  // queued number must be greater than or equal to database-stored number
    {
        latest = queued;
        assert (latest >= stored);
    }

    // TODO: Archiver's internal counter should really be directly updated by post commit

    bool skip = false;

    // 1) Check for local clock lag: is internal counter behind the latest queued / stored epoch sequence combo?
    if (_counter < latest)
    {
        LOG_WARN(_log) << "Archiver::ShouldSkipMBBuild - internal counter epoch:seq="
                       << _counter.first << ":" << _counter.second
                       << ", latest stored/queued epoch:seq=" << latest.first << ":" << latest.second
                       << ", local clock is behind, skipping MB archival proposal.";
        // TODO: sync clock?

        // update internal counter to catch up to latest sequence, skip proposal
        _counter = latest;
        skip = true;
    }

    // 2) Check for unfinished consensus session
    if (is_queued && queued > stored)
    {
        LOG_WARN(_log) << "Archiver::ShouldSkipMBBuild - queued epoch:seq=" << queued.first << ":" << queued.second
                       << ", stored epoch:seq=" << stored.first << ":" << stored.second;
        // If queued (epoch, seq) is more than one ahead of stored (epoch, seq), then the database is out of sync
        if (queued.second > stored.second + 1)
        {
            LOG_ERROR(_log) << "Archiver::ShouldSkipMBBuild - queued sequence is more than 1 ahead of stored. "
                            << "Database is out of sync";
            // TODO: bootstrap
        }
        else
        {
            LOG_WARN(_log) << "Archiver::ShouldSkipMBBuild - ongoing MB consensus is unfinished, "
                           << "skipping MB archival proposal.";
        }
        skip = true;
    }

    // internal counter, stored, and queued (if any) all match: we can go ahead and build the next MB
    return skip;
}

bool
Archiver::IsRecall()
{
    return _recall_handler.IsRecall();
}

EpochHandler &
Archiver::GetEpochHandler()
{
    return _epoch_handler;
}