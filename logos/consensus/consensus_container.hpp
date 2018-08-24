//===-- logos/consensus/consensus_container.hpp - ConsensusContainer class declaration -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the ConsensusContainer class, which encapsulates
/// consensus related types
///
//===----------------------------------------------------------------------===//
#pragma once

#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/batchstateblock/batchblock_consensus_manager.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/consensus/consensus_netio_manager.hpp>
#include <logos/microblock/microblock.hpp>

namespace logos {
    class node_config;
    class state_block;
}

//! Encapsulate consensus related objects
/*!
   This class servers as container for other consensus related
   types. It provides consensus interface to the node object
 */
class ConsensusContainer 
{
    //! Aliases
    using Service          = boost::asio::io_service;
	using Config           = logos::node_config;
    using Log              = boost::log::sources::logger_mt;
    using Store            = logos::block_store;
public:
    //! Class constructor
    /*!
      This must be a singleton instances
      \param service reference to boost asio service 
      \param store  reference to blockstore
      \param alarm  reference to alarm
      \param log reference to boost log
      \param config reference to node_config
    */
    ConsensusContainer(Service & service,
                       Store & store,
                       logos::alarm & alarm,
                       Log & log,
                       const Config & config);

    //! Class destructor
    ~ConsensusContainer() {}

    //! BatchBlock consensus related
    /*!
      Submits transaction to consensus logic
      \param block transaction formated as the state_block
      \param should_buffer bool flag to indicate if the buffering is supported
      \return process_return result of the operation
    */
    logos::process_return OnSendRequest(std::shared_ptr<logos::state_block> block, bool should_buffer);
    //! BatchBlock consensus related
    /*!
      Indicated that the buffering of state blocks is complete
      \param result result of the operation
    */
    void BufferComplete(logos::process_return & result);

    // MicroBlock
    void StartMicroBlock(std::function<void(MicroBlock&)>);

    void BuildMicroBlock(MicroBlock&);

    logos::process_return OnSendRequest(std::shared_ptr<MicroBlock>);

private:
    DelegateKeyStore            _key_store; //!< Delegates public key store
    MessageValidator            _validator; //!< Validator/Signer of consensus messages
    BatchBlockConsensusManager  _batchblock_consensus_manager; //!< Handles batch block consensus handling
	MicroBlockConsensusManager	_microblock_consensus_manager; //!< Handles micro block consensus handling
	EpochConsensusManager	    _epoch_consensus_manager; //!< Handles epoch consensus handling
    ConsensusNetIOManager       _consensus_netio_manager; //!< Establishes connections between the delegates
    MicroBlockHandler           _microblock_handler; //!< Handles microblock processing
};
