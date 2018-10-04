/// @file
/// This file contains implementation of the ConsensusContainer class, which encapsulates
/// consensus related types.
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/node/node.hpp>

uint ConsensusContainer::_cur_epoch_id = 0;

ConsensusContainer::EpochManager::EpochManager(Service & service,
                                               Store & store,
                                               Alarm & alarm,
                                               Log & log,
                                               const Config & config,
                                               Archiver & archiver,
                                               PeerAcceptorStarter & starter)
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
        _key_store, _validator, starter)
{}

ConsensusContainer::ConsensusContainer(Service & service,
                                       Store & store,
                                       logos::alarm & alarm,
                                       Log & log,
                                       const Config & config,
                                       Archiver & archiver)
    : _peer_manager(service, config.consensus_manager_config, std::bind(&ConsensusContainer::PeerBinder, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
    , _cur_epoch(std::make_shared<EpochManager>(service, store, alarm, log, config, archiver, _peer_manager))
    , _tmp_epoch(nullptr)
    , _service(service)
    , _store(store)
    , _alarm(alarm)
    , _config(config)
    , _log(log)
    , _archiver(archiver)
    , _epoch_transition(false)
{
}

logos::process_return 
ConsensusContainer::OnSendRequest(
    std::shared_ptr<logos::state_block> block, 
    bool should_buffer)
{
    logos::process_return result;
    std::lock_guard<std::mutex> lock(_mutex);

	if(!block)
	{
	    result.code = logos::process_result::invalid_block_type;
	    return result;
	}


    using Request = RequestMessage<ConsensusType::BatchStateBlock>;

    if(should_buffer)
    {
        result.code = logos::process_result::buffered;
        _cur_epoch->_batch_manager.OnBenchmarkSendRequest(
            static_pointer_cast<Request>(block), result);
    }
    else
    {
        _cur_epoch->_batch_manager.OnSendRequest(
            static_pointer_cast<Request>(block), result);
    }

    return result;
}

void 
ConsensusContainer::BufferComplete(
    logos::process_return & result)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _cur_epoch->_batch_manager.BufferComplete(result);
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<MicroBlock> block)
{
    std::lock_guard<std::mutex> lock(_mutex);
	using Request = RequestMessage<ConsensusType::MicroBlock>;
	
    logos::process_return result;
    _cur_epoch->_micro_manager.OnSendRequest(
        std::static_pointer_cast<Request>(block), result);;

    return result;
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<Epoch> block)
{
    std::lock_guard<std::mutex> lock(_mutex);
    using Request = RequestMessage<ConsensusType::Epoch>;

    logos::process_return result;
    _cur_epoch->_epoch_manager.OnSendRequest(
            std::static_pointer_cast<Request>(block), result);;

    return result;
}

void
ConsensusContainer::PeerBinder(
    const Endpoint &endpoint,
    std::shared_ptr<Socket> socket,
    std::shared_ptr<KeyAdvertisement> advert)
{
   /// TBD call appropriate epoch depending on the epoch# in advert
   _cur_epoch->_netio_manager.OnConnectionAccepted(endpoint, socket, advert);
}
