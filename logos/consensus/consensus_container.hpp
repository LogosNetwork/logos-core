#pragma once

#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/batchblock_consensus_manager.hpp>
#include <logos/consensus/consensus_netio.hpp>
//#include <logos/microblock/microblock.hpp>

namespace logos {
    class node_config;
    class state_block;
}

/// Encapsulates consensus related objects
/// Provides consensus interface to the node object
class ConsensusContainer 
{
    using Service          = boost::asio::io_service;
	using Config           = logos::node_config;
    using Log              = boost::log::sources::logger_mt;
    using Store            = logos::block_store;
public:
    ConsensusContainer(Service & service,
                       Store & store,
                       logos::alarm & alarm,
                       Log & log,
                       const Config & config);

    ~ConsensusContainer() {}

    // BatchBlock
    logos::process_return OnSendRequest(std::shared_ptr<logos::state_block> block, bool should_buffer);
    void BufferComplete(logos::process_return & result);

    // MicroBlock
    //void StartMicroBlock(std::function<void(MicroBlock&)>);

private:
    DelegateKeyStore            _key_store;
    MessageValidator            _validator;
    BatchBlockConsensusManager  _batchblock_consensus_manager;
    ConsensusNetIOManager       _consensus_netio_manager;
    //MicroBlockHandler           _micro_block_handler;
};
