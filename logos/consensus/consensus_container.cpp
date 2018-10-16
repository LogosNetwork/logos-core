/// @file
/// This file contains implementation of the ConsensusContainer class, which encapsulates
/// consensus related types.
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/node/node_identity_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/node/node.hpp>

std::atomic_uint ConsensusContainer::_cur_epoch_number(0);

ConsensusContainer::EpochManager::EpochManager(Service & service,
                                               Store & store,
                                               Alarm & alarm,
                                               Log & log,
                                               const Config & config,
                                               Archiver & archiver,
                                               PeerAcceptorStarter & starter,
                                               ConnectingDelegatesSet delegates_set)
    : _validator(_key_store)
    , _batch_manager(service, store, log,
                 config, _key_store, _validator)
    , _micro_manager(service, store, log,
                 config, _key_store, _validator, archiver)
    , _epoch_manager(service, store, log,
                 config, _key_store, _validator, archiver)
    , _delegates_set(delegates_set)
    , _netio_manager(
        {
                {ConsensusType::BatchStateBlock, _batch_manager},
                {ConsensusType::MicroBlock, _micro_manager},
                {ConsensusType::Epoch, _epoch_manager}
        },
        service, alarm, config,
        _key_store, _validator, starter, _delegates_set)
{}

ConsensusContainer::ConsensusContainer(Service & service,
                                       Store & store,
                                       logos::alarm & alarm,
                                       Log & log,
                                       const Config & config,
                                       Archiver & archiver,
                                       NodeIdentityManager & identity_manager)
    : _peer_manager(service, config, std::bind(&ConsensusContainer::PeerBinder, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
    , _cur_epoch(nullptr)
    , _trans_epoch(nullptr)
    , _service(service)
    , _store(store)
    , _alarm(alarm)
    , _config(config)
    , _log(log)
    , _archiver(archiver)
    , _identity_manager(identity_manager)
    , _transition_state(EpochTransitionState::None)
    , _transition_delegate(EpochTransitionDelegate::None)
{
    // a node can start just a few seconds before the transition TODO

    // Is this instance "epoch transition enabled"?
    // Currently require that all_delegates is twice the size of delegates
    bool epoch_transition_enabled = true;
    if (config.all_delegates.size() != 2 * config.delegates.size())
    {
        epoch_transition_enabled = false;
    }

    uint8_t delegate_idx;
    Accounts delegates;
    _identity_manager.IdentifyDelegates(EpochDelegates::Current, delegate_idx, delegates);

    bool in_epoch = delegate_idx != NON_DELEGATE;

    if (!epoch_transition_enabled)
    {
        _cur_epoch = std::make_shared<EpochManager>(service, store, alarm, log, config, archiver, _peer_manager,
            ConnectingDelegatesSet::Current);
    }
    else if (in_epoch)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, delegates);
        _cur_epoch = std::make_shared<EpochManager>(service, store, alarm, log, epoch_config, archiver, _peer_manager,
            ConnectingDelegatesSet::Current);
    }

    BOOST_LOG(_log) << "ConsensusContainer::ConsensusContainer initiated delegate "
                    << (int)delegate_idx << " " << (int)NodeIdentityManager::_global_delegate_idx
                    << " in epoch " << in_epoch << " epoch transition enabled " << epoch_transition_enabled;
}

logos::process_return 
ConsensusContainer::OnSendRequest(
    std::shared_ptr<logos::state_block> block, 
    bool should_buffer)
{
    logos::process_return result;
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_implemented;
        BOOST_LOG(_log) << "ConsensusContainer::OnSendRequest transaction, the node is not a delegate, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return result;
    }

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
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_implemented;
        BOOST_LOG(_log) << "ConsensusContainer::OnSendRequest transaction, the node is not a delegate, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return;
    }

    _cur_epoch->_batch_manager.BufferComplete(result);
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<MicroBlock> block)
{
    OptLock lock(_transition_state, _mutex);
    logos::process_return result;
	using Request = RequestMessage<ConsensusType::MicroBlock>;

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_implemented;
        BOOST_LOG(_log) << "ConsensusContainer::OnSendRequest microblock, the node is not a delegate, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return result;
    }

    _cur_epoch->_micro_manager.OnSendRequest(
        std::static_pointer_cast<Request>(block), result);;

    return result;
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<Epoch> block)
{
    OptLock lock(_transition_state, _mutex);
    logos::process_return result;
    using Request = RequestMessage<ConsensusType::Epoch>;

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_implemented;
        BOOST_LOG(_log) << "ConsensusContainer::OnSendRequest epoch, the node is not a delegate, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return result;
    }

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
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr && _trans_epoch == nullptr)
    {
        socket->close();
        BOOST_LOG(_log) << "ConsensusContainer::PeerBinder: the node is not accepting connections, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return;
    }

    // due to the clock drift another delegate is already in the epoch transition state,
    // while this delegate has not received yet the epoch transition event - queue up the connection
    // for binding to the right instance of EpochManager once the epoch transition event is received
    if (advert->delegates_set == ConnectingDelegatesSet::New && _transition_state == EpochTransitionState::None)
    {
        _connections_queue.push(ConnectionCache{socket, advert, endpoint});
        BOOST_LOG(_log) << "ConsensusContainer::PeerBinder: epoch transition has not started yet, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return;
    }

    bool bind_to_cur = true;

    // Need to confirm if all use cases are covered TODO
    if (advert->delegates_set == ConnectingDelegatesSet::New &&
            _transition_state == EpochTransitionState::Connecting &&
            _transition_delegate != EpochTransitionDelegate::Retiring ||
        (advert->delegates_set == ConnectingDelegatesSet::Current ||
         advert->delegates_set == ConnectingDelegatesSet::Outgoing) &&
            (_transition_state == EpochTransitionState::Connecting &&
             _transition_delegate == EpochTransitionDelegate::New ||
             _transition_state != EpochTransitionState::Connecting &&
             _transition_delegate == EpochTransitionDelegate::Persistent))
    {
        bind_to_cur = false;
    }

    if (bind_to_cur)
    {
        BOOST_LOG(_log) << "ConsensusContainer::PeerBinder, binding to current epoch, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        _cur_epoch->_netio_manager.OnConnectionAccepted(endpoint, socket, advert);
    }
    else
    {
        if (_trans_epoch == nullptr)
        {
            BOOST_LOG(_log) << "ConsensusContainer::PeerBinder: second set of consensus delegates is null, "
                            << (int)NodeIdentityManager::_global_delegate_idx;
            return;
        }
        BOOST_LOG(_log) << "ConsensusContainer::PeerBinder, binding to transition epoch, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        _trans_epoch->_netio_manager.OnConnectionAccepted(endpoint, socket, advert);
    }
}

void
ConsensusContainer::EpochTransitionEventsStart()
{
    std::lock_guard<std::mutex> lock(_mutex);
    uint8_t delegate_idx;
    Accounts delegates;

    _identity_manager.IdentifyDelegates(EpochDelegates::Next, delegate_idx, delegates);


    // no action - this node is not in the current or next epoch
    if (delegate_idx == NON_DELEGATE && _cur_epoch == nullptr)
    {
        BOOST_LOG(_log) << "ConsensusContainer::EpochTransitionEventsStart not a delegate in current or next epoch, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return;
    }

    if (delegate_idx == NON_DELEGATE)
    {
        _transition_delegate = EpochTransitionDelegate::Retiring;
    }
    else if (_cur_epoch == nullptr)
    {
        _transition_delegate = EpochTransitionDelegate::New;
    }
    else
    {
        _transition_delegate = EpochTransitionDelegate::Persistent;
    }

    _transition_state = EpochTransitionState::Connecting;

    BOOST_LOG(_log) << "ConsensusContainer::EpochTransitionEventsStart : delegate "
                    << TransitionDelegateToName(_transition_delegate) <<  ' '
                    << TransitionStateToName(_transition_state) << " index "
                    << (int)delegate_idx << " global " << (int)NodeIdentityManager::_global_delegate_idx;

    if (_transition_delegate != EpochTransitionDelegate::Retiring)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, delegates);
        _trans_epoch = std::make_shared<EpochManager>(_service, _store,
            _alarm, _log, epoch_config, _archiver, _peer_manager, ConnectingDelegatesSet::New);
    }

    _alarm.add(Seconds(5), std::bind(&ConsensusContainer::BindConnectionsQueue, this));

    _alarm.add(EPOCH_DELEGATES_CONNECT-EPOCH_TRANSITION_START,
        std::bind(&ConsensusContainer::EpochTransitionStart, this, delegate_idx));
}

void
ConsensusContainer::BindConnectionsQueue()
{
    while (_connections_queue.size() > 0)
    {
        auto element = _connections_queue.front();
        _connections_queue.pop();
        _alarm.add(std::chrono::steady_clock::now(), [this, element]()mutable->void{
            PeerBinder(element.endpoint, element.socket, element.advert);
        });
    }
}

void
ConsensusContainer::EpochTransitionStart(uint8_t delegate_idx)
{
    std::lock_guard<std::mutex>   lock(_mutex);
    BOOST_LOG(_log) << "ConsensusContainer::EpochTransitionStart: "
                    << (int)delegate_idx
                    << " " << (int)NodeIdentityManager::_global_delegate_idx;

    if (_transition_delegate == EpochTransitionDelegate::Persistent)
    {
        _cur_epoch.swap(_trans_epoch);
        _cur_epoch->UpdateRequestPromoter();
    }
    else if (_transition_delegate == EpochTransitionDelegate::New)
    {
        _cur_epoch = _trans_epoch;
        _trans_epoch = nullptr;
    }

    _transition_state = EpochTransitionState::EpochTransitionStart;

    _alarm.add(EPOCH_START,
               std::bind(&ConsensusContainer::EpochStart, this, delegate_idx));
}

void
ConsensusContainer::EpochStart(uint8_t delegate_idx)
{
    BOOST_LOG(_log) << "ConsensusContainer::EpochStart: " << (int)delegate_idx << " "
                    << (int)NodeIdentityManager::_global_delegate_idx;

    _alarm.add(EPOCH_TRANSITION_END,
               std::bind(&ConsensusContainer::EpochTransitionEnd, this, delegate_idx));

    if ((_transition_delegate == EpochTransitionDelegate::Persistent ||
        _transition_delegate == EpochTransitionDelegate::Retiring) && _trans_epoch != nullptr)
    {
        // can be used as a flag to not attempt reconnection for the outgoing delegate set
        // when end of file is received on the socket
        _trans_epoch->_delegates_set = ConnectingDelegatesSet::Outgoing;
    }

    _transition_state = EpochTransitionState::EpochStart;

    _cur_epoch_number++;
}

void
ConsensusContainer::EpochTransitionEnd(uint8_t delegate_idx)
{
    bool retired = false;

    _trans_epoch = nullptr;

    if (_transition_delegate == EpochTransitionDelegate::Retiring)
    {
        _cur_epoch = nullptr;
        retired = true;
    }
    else
    {
        _cur_epoch->_delegates_set = ConnectingDelegatesSet::Current;
    }

    BOOST_LOG(_log) << "ConsensusContainer::EpochTransitionEnd : retired " << retired << " "
                    << (int)delegate_idx << " " << " " << (int)NodeIdentityManager::_global_delegate_idx;

    _transition_state = EpochTransitionState::None;
    _transition_delegate = EpochTransitionDelegate::None;
}

ConsensusManagerConfig
ConsensusContainer::BuildConsensusConfig(
    const uint8_t delegate_idx,
    const Accounts & delegates)
{
   ConsensusManagerConfig config = _config;

   config.delegate_id = delegate_idx;
   config.local_address = NodeIdentityManager::_delegates_ip[NodeIdentityManager::_delegate_account];
   config.delegates.clear();

   for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
   {
        auto account = delegates[del];
        auto ip = NodeIdentityManager::_delegates_ip[account];
        config.delegates.push_back(ConsensusManagerConfig::Delegate{ip, del});
   }

   return config;
}
