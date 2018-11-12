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
    virtual bool Validate(const MicroBlock&) = 0;
    virtual void CommitToDatabase(const MicroBlock&) = 0;
};

class ArchiverEpochHandler
{
public:
    ArchiverEpochHandler() = default;
    virtual ~ArchiverEpochHandler() = default;
    virtual bool Validate(const Epoch&) = 0;
    virtual void CommitToDatabase(const Epoch&) = 0;
};

/// Container for Epoch/MicroBlock handling, Event proposing, Voting manager, and
/// Recall handler
/// Archiver
/// - starts MicroBlock and Epoch Transition timers
/// - provides bridge between consensus and handlers via IArchiverMicroBlockHandler
///     and IArchiverEpochHandler, where each handler implements validation
///     and database update
/// - ties handlers to EventProposer; i.e. when the last MicroBlock is committed to
///   the database, the archiver calls EventProposer::ProposeEpoch to start
///   creation of the Epoch block
/// - interfaces to VotingManager to validate delegates in the proposed Epoch block
///   and fetch delegates for the proposed Epoch block
/// - interfaces to recall handler to check whether a recall happened in the
///   current epoch
class Archiver : public ArchiverEpochHandler,
                 public ArchiverMicroBlockHandler
{
    friend MicroBlockTester;

    using MicroBlockConsensusCb = std::function<logos::process_return(std::shared_ptr<MicroBlock>)>;
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

    /// Validate Micro block
    /// @param block micro block to validate
    /// @returns true if validated
    bool Validate(const MicroBlock& block) override
    {
        return _micro_block_handler.Validate(block);
    }

    /// Commit micro block to the database, propose epoch
    /// @param block to commit
    void CommitToDatabase(const MicroBlock& block) override
    {
        _micro_block_handler.ApplyUpdates(block);
        if (block.last_micro_block) {
            _event_proposer.ProposeEpoch();
        }
    }

    /// Validate Epoch block
    /// @param block to validate
    /// @returns true if validated
    bool Validate(const Epoch& block) override
    {
        return _epoch_handler.Validate(block);
    }

    /// Commit epoch block to the database
    /// @param block to commit
    void CommitToDatabase(const Epoch& block) override
    {
        _epoch_handler.ApplyUpdates(block);
    }

private:
    static constexpr uint8_t SELECT_PRIMARY_DELEGATE = 0x1F;

    /// Used by MicroBlockTester to start microblock generation
    /// @param consensus send microblock for consensus [in]
    /// @param last_microblock last microblock flag
    void Test_ProposeMicroBlock(InternalConsensus&, bool last_microblock);

    /// Is this the first Epoch
    /// @param store reference to block store
    bool IsFirstEpoch(BlockStore &store);

    bool                _first_epoch;
    EpochVotingManager  _voting_manager;
    EventProposer       _event_proposer;
    MicroBlockHandler   _micro_block_handler;
    EpochHandler        _epoch_handler;
    IRecallHandler &    _recall_handler;
    logos::block_store &_store;
    Log                 _log;
};