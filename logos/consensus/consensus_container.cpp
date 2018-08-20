//===-- logos/consensus/consensus_container.cpp - ConsensusContainer class implementation -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementation of the ConsensusContainer class, which encapsulates
/// consensus related types
///
//===----------------------------------------------------------------------===//
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/node/node.hpp>

ConsensusContainer::ConsensusContainer(Service & service,
                                       Store & store,
                                       logos::alarm & alarm,
                                       Log & log,
                                       const Config & config)
    : _validator(_key_store)
    , _batchblock_consensus_manager(service, store, alarm, log, 
            config.consensus_manager_config, _key_store, _validator)
    , _microblock_consensus_manager(service, store, alarm, log, 
            config.consensus_manager_config, _key_store, _validator)
    , _consensus_netio_manager(
        {
            {ConsensusType::BatchStateBlock, _batchblock_consensus_manager},
            {ConsensusType::MicroBlock, _microblock_consensus_manager}
        }, 
        service, alarm, config.consensus_manager_config, _key_store, _validator)
    , _microblock_handler(alarm, store, NUM_DELEGATES, config.microblock_generation_interval)
{
}

logos::process_return 
ConsensusContainer::OnSendRequest(
    std::shared_ptr<logos::state_block> block, 
    bool should_buffer)
{
    logos::process_return result;

	if(!block)
	{
	    result.code = logos::process_result::invalid_block_type;
	    return result;
	}

	if(should_buffer)
	{
        result.code = logos::process_result::buffered;
	    _batchblock_consensus_manager.OnBenchmarkSendRequest(
            static_pointer_cast<RequestMessage<ConsensusType::BatchStateBlock>>(block), result);
	}
	else
	{
        _batchblock_consensus_manager.OnSendRequest(
            static_pointer_cast<RequestMessage<ConsensusType::BatchStateBlock>>(block), result);
	}

	return result;
}

void 
ConsensusContainer::BufferComplete(
    logos::process_return & result)
{
    _batchblock_consensus_manager.BufferComplete(result);
}

void 
ConsensusContainer::StartMicroBlock(
    std::function<void(MicroBlock&)> cb)
{
    _microblock_handler.Start(cb);
}