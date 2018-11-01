/// @file
/// This file contains implementation of the ConsensusContainer class, which encapsulates
/// consensus related types.
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/node/node.hpp>

ConsensusContainer::ConsensusContainer(Service & service,
                                       Store & store,
                                       logos::alarm & alarm,
                                       Log & log,
                                       const Config & config,
                                       Archiver & archiver)
    : _validator(_key_store)
    , _batch_manager(service, store, log,
            config.consensus_manager_config, _key_store, _validator)
    , _micro_manager(service, store, log,
            config.consensus_manager_config, _key_store, _validator, archiver)
    , _epoch_manager(service, store, log,
            config.consensus_manager_config, _key_store, _validator, archiver)
    , _netio_manager(
        {
            {ConsensusType::BatchStateBlock, _batch_manager},
            {ConsensusType::MicroBlock, _micro_manager},
            {ConsensusType::Epoch, _epoch_manager}
        },
        service, alarm, config.consensus_manager_config,
		_key_store, _validator)
{}

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


    using Request = RequestMessage<ConsensusType::BatchStateBlock>;

    if(should_buffer)
    {
        result.code = logos::process_result::buffered;
        _batch_manager.OnBenchmarkSendRequest(
            static_pointer_cast<Request>(block), result);
    }
    else
    {
        _batch_manager.OnSendRequest(
            static_pointer_cast<Request>(block), result);
    }

    return result;
}

void 
ConsensusContainer::BufferComplete(
    logos::process_return & result)
{
    _batch_manager.BufferComplete(result);
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<MicroBlock> block)
{
	using Request = RequestMessage<ConsensusType::MicroBlock>;
	
    logos::process_return result;
    _micro_manager.OnSendRequest(
        std::static_pointer_cast<Request>(block), result);;

    return result;
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<Epoch> block)
{
    using Request = RequestMessage<ConsensusType::Epoch>;

    logos::process_return result;
    _epoch_manager.OnSendRequest(
            std::static_pointer_cast<Request>(block), result);;

    return result;
}
