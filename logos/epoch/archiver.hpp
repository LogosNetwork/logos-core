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

namespace logos
{
    class alarm;
    class block_store;
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
    /// @param alarm logos alarm reference [in]
    /// @param store logos block_store reference [in]
    /// @param recall recall handler interface [in]
    Archiver(logos::alarm&, BlockStore&, IRecallHandler &);
    ~Archiver() = default;

    /// Start archiving events
    /// @param internal consensus interface reference
    void Start(InternalConsensus&);

    /// Commit micro block to the database, propose epoch
    /// @param block to commit
    void OnApplyUpdates(const ApprovedMB &block) override
    {
        if (block.last_micro_block) {
            _event_proposer.ProposeEpoch();
        }
    }

    /// Is Recall
    /// @returns true if recall
    bool IsRecall();

    void CacheElectionWinners(std::vector<std::pair<AccountAddress,Amount>>& winners);

private:
    static constexpr uint8_t SELECT_PRIMARY_DELEGATE = 0x1F;

    /// Used by MicroBlockTester to start microblock generation
    /// @param consensus send microblock for consensus [in]
    /// @param last_microblock last microblock flag
    void Test_ProposeMicroBlock(InternalConsensus&, bool last_microblock);

    /// Is this the first Epoch
    /// @param store reference to block store
    bool IsFirstEpoch(BlockStore &store);

    /// Is this the first MicroBlock
    /// @param store reference to block store
    bool IsFirstMicroBlock(BlockStore &store);

    bool                _first_epoch;
    EpochVotingManager  _voting_manager;
    EventProposer       _event_proposer;
    MicroBlockHandler   _micro_block_handler;
    EpochHandler        _epoch_handler;
    IRecallHandler &    _recall_handler;
    logos::block_store &_store;
    Log                 _log;
};