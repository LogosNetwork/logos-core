/// @file
/// This file contains the declaration of the ConsensusContainer class, which encapsulates
/// consensus related types
#pragma once

#include <logos/consensus/batchblock/batchblock_consensus_manager.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/consensus/network/consensus_netio_manager.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>

namespace logos
{
    class node_config;
    class state_block;
}

class Archiver;

class InternalConsensus
{
public:
    InternalConsensus() = default;
    virtual ~InternalConsensus() = default;
    virtual logos::process_return OnSendRequest(std::shared_ptr<MicroBlock>) = 0;
    virtual logos::process_return OnSendRequest(std::shared_ptr<Epoch>) = 0;
};

/// Encapsulates consensus related objects.
///
/// This class serves as a container for ConsensusManagers
/// and other consensus-related types and provides an interface
/// to the node object.
class ConsensusContainer : public InternalConsensus
{

    using Service = boost::asio::io_service;
    using Config  = logos::node_config;
    using Store   = logos::block_store;

public:

    /// Class constructor.
    ///
    /// This must be a singleton instance.
    ///     @param[in] service reference to boost asio service
    ///     @param[in] store reference to blockstore
    ///     @param[in] alarm reference to alarm
    ///     @param[in] log reference to boost log
    ///     @param[in] config reference to node_config
    ///     @param[in] archiver epoch/microblock related consensus validation and persistence
    ConsensusContainer(Service & service,
                       Store & store,
                       logos::alarm & alarm,
                       const Config & config,
                       Archiver & archiver);

    ~ConsensusContainer() = default;

    /// Handles requests for batch block consensus.
    ///
    /// Submits transactions to consensus logic.
    ///     @param[in] block state_block containing the transaction
    ///     @param[in] should_buffer bool flag that, when set, will
    ///                              cause the block to be buffered
    ///     @return process_return result of the operation
    logos::process_return OnSendRequest(std::shared_ptr<logos::state_block> block,
                                        bool should_buffer);

    /// Called when buffering is done for batch block consensus.
    ///
    /// Indicates that the buffering of state blocks is complete.
    ///     @param[out] result result of the operation
    void BufferComplete(logos::process_return & result);

protected:

	/// Initiate MicroBlock consensus
	///		@param[in] MicroBlock containing the batch blocks
    logos::process_return OnSendRequest(std::shared_ptr<MicroBlock>) override;

    /// Initiate MicroBlock consensus
    ///		@param[in] Epoch containing the microblocks
    logos::process_return OnSendRequest(std::shared_ptr<Epoch>) override;

private:

    DelegateKeyStore           _key_store; 	   ///< Store delegates public keys
    MessageValidator           _validator; 	   ///< Validator/Signer of consensus messages
    BatchBlockConsensusManager _batch_manager; ///< Handles batch block consensus
	MicroBlockConsensusManager _micro_manager; ///< Handles micro block consensus
	EpochConsensusManager	   _epoch_manager; ///< Handles epoch consensus
    ConsensusNetIOManager      _netio_manager; ///< Establishes connections to other delegates
};
