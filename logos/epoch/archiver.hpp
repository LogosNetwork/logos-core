///
/// @file
/// This file contains declaration of the Archiver class - container for epoch/microblock handling related classes
///
#pragma once
#include <logos/microblock/microblock_handler.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/epoch/event_proposer.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/consensus/primary_delegate.hpp>

class IInternalConsensusCb;
class IRecallHandler;
class MicroBlockTester;

namespace logos
{
    class alarm;
    class block_store;
}

class IArchiverMicroBlockHandler
{
public:
    using EpochConsensusCb = std::function<logos::process_return(std::shared_ptr<Epoch>)>;
    IArchiverMicroBlockHandler() = default;
    virtual ~IArchiverMicroBlockHandler() = default;
    virtual bool Validate(const MicroBlock&) = 0;
    virtual void CommitToDatabase(const MicroBlock&) = 0;
};

class IArchiverEpochHandler
{
public:
    IArchiverEpochHandler() = default;
    virtual ~IArchiverEpochHandler() = default;
    virtual bool Validate(const Epoch&) = 0;
    virtual void CommitToDatabase(const Epoch&) = 0;
};

/// Container for epoch/microblock handling related classes
class Archiver : public IArchiverEpochHandler,
                 public IArchiverMicroBlockHandler
{
    friend MicroBlockTester;

    using Log                   = boost::log::sources::logger_mt;
    using MicroBlockConsensusCb = std::function<logos::process_return(std::shared_ptr<MicroBlock>)>;
public:
    /// Class constructor
    /// @param alarm logos alarm reference [in]
    /// @param store logos block_store reference [in]
    /// @param recall recall handler interface [in]
    /// @param delegate_id this delegate id [in]
    Archiver(logos::alarm&, BlockStore&, IRecallHandler &, uint8_t);
    ~Archiver() = default;

    /// Start archiving events
    /// @param internal consensus interface reference
    void Start(IInternalConsensusCb&);

    /// Validate Micro block
    /// @param block micro block to validate
    /// @returns true if validated
    bool Validate(const MicroBlock& block) override
    {
        return _micro_block_handler.VerifyMicroBlock(block);
    }

    /// Commit micro block to the database, propose epoch
    /// @param block to commit
    void CommitToDatabase(const MicroBlock& block) override
    {
        _micro_block_handler.ApplyUpdates(block);
        if (block._last_micro_block) {
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

    /// Create genesis Epoch's and MicroBlock's
    void CreateGenesisBlocks(logos::transaction &transaction);

private:
    static constexpr uint8_t SELECT_PRIMARY_DELEGATE = 0x1F;

    /// Used by MicroBlockTester to start microblock generation
    /// @param consensus send microblock for consensus [in]
    /// @param last_microblock last microblock flag
    void Test_ProposeMicroBlock(IInternalConsensusCb&, bool last_microblock);

    /// Is this primary delegate
    /// @param hash of the previous block
    /// @returns true if primary delegate
    bool IsPrimaryDelegate(const BlockHash &hash)
    {
        uint8_t primary_delegate = (uint8_t)(hash.bytes[0] & SELECT_PRIMARY_DELEGATE) % PrimaryDelegate::QUORUM_SIZE;
        return (primary_delegate == _delegate_id);
    }

    EpochVotingManager  _voting_manager;
    EventProposer       _event_proposer;
    MicroBlockHandler   _micro_block_handler;
    EpochHandler        _epoch_handler;
    IRecallHandler &     _recall_handler;
    logos::block_store &_store;
    uint8_t             _delegate_id;
    Log                 _log;
};