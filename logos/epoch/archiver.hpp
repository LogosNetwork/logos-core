///
/// @file
/// This file contains declaration of the Archiver class - container for epoch/microblock handling related classes
///
#pragma once
#include <logos/microblock/microblock_handler.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/epoch/event_proposer.hpp>
#include <logos/epoch/epoch_handler.hpp>

class InternalConsensus;
class IRecallHandler;
class MicroBlockTester;
class MicroBlockMessageHandler;

namespace logos
{
    class alarm;
    class block_store;
    class IBlockCache;
}

class ArchiverMicroBlockHandler
{
public:
    using EpochConsensusCb = std::function<logos::process_return(std::shared_ptr<Epoch>)>;

    ArchiverMicroBlockHandler() = default;
    virtual ~ArchiverMicroBlockHandler() = default;
    virtual void OnApplyUpdates(const ApprovedMB &) = 0;
};

/// Container for Epoch/MicroBlock handling, Event proposing, Voting manager, and
/// Recall handler
/// Archiver
/// - starts MicroBlock and Epoch Transition timers
/// - provides database update handler via IArchiverMicroBlockHandler
/// - ties handlers to EventProposer; i.e. when the last MicroBlock is committed to
///   the database, the archiver calls EventProposer::ProposeEpoch to start
///   creation of the Epoch block
/// - interfaces to VotingManager to validate delegates in the proposed Epoch block
///   and fetch delegates for the proposed Epoch block
/// - interfaces to recall handler to check whether a recall happened in the
///   current epoch
class Archiver : public ArchiverMicroBlockHandler
{
    friend MicroBlockTester;

public:
    /// Class constructor
    /// @param[in] alarm logos alarm reference
    /// @param[in] store logos block_store reference
    /// @param[in] event proposer reference
    /// @param[in] recall recall handler interface reference
    /// @param[in] block cache interface reference
    Archiver(logos::alarm &, BlockStore &, EventProposer &, IRecallHandler &, logos::IBlockCache &);
    ~Archiver() = default;

    /// Start archiving events
    /// @param internal consensus interface reference
    void Start(InternalConsensus&);

    // Stop archiving events (epoch transition event still continues)
    void Stop();

    /// Commit micro block to the database, propose epoch
    /// @param block to commit
    void OnApplyUpdates(const ApprovedMB &block) override;

    /// Is Recall
    /// @returns true if recall
    bool IsRecall();

    EpochHandler & GetEpochHandler();

private:
    static constexpr uint8_t SELECT_PRIMARY_DELEGATE = 0x1F;

    /// Used by MicroBlockTester to start microblock generation
    /// @param[in] consensus send microblock for consensus
    /// @param[in] last_microblock last microblock flag
    void Test_ProposeMicroBlock(InternalConsensus&, bool last_microblock);

    /// Archive MicroBlock, if a new one should be built
    /// This method is passed to EventProposer as a scheduled action
    /// @param internal consensus interface reference
    void ArchiveMB(InternalConsensus &);

    /// Should we skip building a new MB?
    /// This method is called by ArchiveMB
    /// @return true if we should skip build and proposal, either because we are behind
    /// or an ongoing MB consensus session is not finished
    bool ShouldSkipMBBuild();

    EpochSeq                   _counter;        ///< indicates the most recently BUILT MB's <epoch number, sequence number>
    EpochVotingManager         _voting_manager;
    EventProposer &            _event_proposer;
    MicroBlockHandler          _micro_block_handler;
    EpochHandler               _epoch_handler;
    MicroBlockMessageHandler & _mb_message_handler;
    IRecallHandler &           _recall_handler;
    logos::block_store &       _store;
    logos::IBlockCache &       _block_cache;
    Log                        _log;

    friend class Archival_ShouldSkipMBProposal_Test;
};