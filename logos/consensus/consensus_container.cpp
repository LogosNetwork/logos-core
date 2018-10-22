/// @file
/// This file contains implementation of the ConsensusContainer class, which encapsulates
/// consensus related types.
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/node/node_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/node/node.hpp>

std::atomic_uint ConsensusContainer::_cur_epoch_number(0);

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

    // is the node a delegate in this epoch
    bool in_epoch = delegate_idx != NON_DELEGATE;

    auto create = [this](const ConsensusManagerConfig &epoch_config) mutable -> void {
        std::lock_guard<std::mutex> lock(_mutex);
        _cur_epoch = CreateEpochManager(_cur_epoch_number, epoch_config, EpochTransitionDelegate::None,
                                        EpochConnection::Current);
        _binding_map[EpochConnection::Current] = _cur_epoch;
    };

    if (!epoch_transition_enabled)
    {
        create(config);
    }
    else if (in_epoch)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, delegates);
        create(epoch_config);
    }

    BOOST_LOG(_log) << "ConsensusContainer::ConsensusContainer initiated delegate "
                    << (int)delegate_idx << " " << (int)NodeIdentityManager::_global_delegate_idx
                    << " in epoch " << in_epoch << " epoch transition enabled " << epoch_transition_enabled;
}

std::shared_ptr<EpochManager>
ConsensusContainer::CreateEpochManager(
    uint epoch_number,
    const ConsensusManagerConfig &config,
    EpochTransitionDelegate delegate,
    EpochConnection connection )
{
    return std::make_shared<EpochManager>(_service, _store, _alarm, _log, config,
                                          _archiver, _peer_manager, _transition_state,
                                          delegate, connection, epoch_number,
                                          std::bind(&ConsensusContainer::OnNewEpochPostCommit, this),
                                          std::bind(&ConsensusContainer::OnNewEpochReject, this));
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
    const Endpoint endpoint,
    std::shared_ptr<Socket> socket,
    std::shared_ptr<KeyAdvertisement> advert)
{
    std::lock_guard<std::mutex> lock(_mutex);

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
    if (advert->connection == EpochConnection::Transitioning && _transition_state == EpochTransitionState::None)
    {
        _connections_queue.push(ConnectionCache{socket, advert, endpoint});
        BOOST_LOG(_log) << "ConsensusContainer::PeerBinder: epoch transition has not started yet, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return;
    }

    if (advert->connection == EpochConnection::WaitingDisconnect)
    {
        BOOST_LOG(_log) << "ConsensusContainer::PeerBinder will not connect retiring delegate";
        return;
    }

    if (_binding_map.find(advert->connection) == _binding_map.end())
    {
        BOOST_LOG(_log) << "ConsensusContainer::PeerBinder epoch manager is not available for "
                        << TransitionConnectionToName(advert->connection);
        return;
    }

    BOOST_LOG(_log) << "ConsensusContainer::PeerBinder, binding connection "
                    << TransitionConnectionToName(advert->connection) << " to "
                    << _binding_map[advert->connection]->GetConnectionName()
                    << " delegate " << _binding_map[advert->connection]->GetDelegateName()
                    << " state " << _binding_map[advert->connection]->GetStateName()
                    << " " << (int)NodeIdentityManager::_global_delegate_idx;

    _binding_map[advert->connection]->_netio_manager.OnConnectionAccepted(endpoint, socket, advert);
}

void
ConsensusContainer::EpochTransitionEventsStart()
{
    std::lock_guard<std::mutex> lock(_mutex);
    uint8_t delegate_idx;
    Accounts delegates;

    _identity_manager.IdentifyDelegates(EpochDelegates::Next, delegate_idx, delegates);

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
        _trans_epoch = CreateEpochManager(_cur_epoch_number+1, epoch_config, _transition_delegate,
                                          EpochConnection::Transitioning);

        if (_transition_delegate == EpochTransitionDelegate::Persistent)
        {
            if (_cur_epoch == nullptr)
            {
                BOOST_LOG(_log) << "ConsensusContainer::EpochTransitionEventsStart current epoch is null";
                return;
            }
            _cur_epoch->_delegate = EpochTransitionDelegate::Persistent;
        }

        // New and Persistent delegates in the new delegate's set
        _binding_map[EpochConnection::Transitioning] = _trans_epoch;
    }
    else
    {
        _cur_epoch->_delegate = EpochTransitionDelegate::Retiring;
    }

    _alarm.add(Seconds(5), std::bind(&ConsensusContainer::BindConnectionsQueue, this));

    // TODO recall may have different timers
    auto lapse = EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START;
    EpochTimeUtil util;
    auto epoch_start = util.GetNextEpochTime();
    if (epoch_start > EPOCH_TRANSITION_START && epoch_start < lapse)
    {
        lapse = epoch_start - EPOCH_TRANSITION_START;
    }
    else if (epoch_start < EPOCH_TRANSITION_START)
    {
        lapse = epoch_start;
    }

    _alarm.add(lapse, std::bind(&ConsensusContainer::EpochTransitionStart, this, delegate_idx));
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

    _transition_state = EpochTransitionState::EpochTransitionStart;

    if (_transition_delegate == EpochTransitionDelegate::New)
    {
        if (_trans_epoch == nullptr || _cur_epoch != nullptr)
        {
            BOOST_LOG(_log) << "ConsensusContainer::EpochTransitionStart trans epoch is null "
                            << (_trans_epoch == nullptr) << " cur epoch is not null "
                            << (_cur_epoch != nullptr);
        }
        _cur_epoch = _trans_epoch;
        _trans_epoch = nullptr;
    }

    EpochTimeUtil util;
    auto epoch_start = util.GetNextEpochTime();
    auto lapse = epoch_start < EPOCH_START ? epoch_start : EPOCH_START;

    _alarm.add(lapse, std::bind(&ConsensusContainer::EpochStart, this, delegate_idx));
}

bool
ConsensusContainer::OnNewEpochPostCommit()
{
    std::lock_guard<std::mutex>   lock(_mutex);

    if (_cur_epoch == nullptr || _trans_epoch == nullptr)
    {
        BOOST_LOG(_log) << "ConsensusContainer::PersistentToReject trans epoch is null "
                        << (_trans_epoch == nullptr) << " cur epoch is null " << (_cur_epoch == nullptr);
        return false;
    }

    if (_trans_epoch->_delegate != EpochTransitionDelegate::PersistentReject)
    {
        _cur_epoch.swap(_trans_epoch);
        _cur_epoch->UpdateRequestPromoter();
        _trans_epoch->_delegate = EpochTransitionDelegate::PersistentReject;
    }
    _trans_epoch->_connection_state = EpochConnection::WaitingDisconnect;
    _binding_map.erase(EpochConnection::Current);

    return true;
}

bool
ConsensusContainer::OnNewEpochReject()
{
    std::lock_guard<std::mutex>   lock(_mutex);

    if (_cur_epoch == nullptr)
    {
        BOOST_LOG(_log) << "ConsensusContainer::RetiringToForwardOnly cur epoch is null";
        return false;
    }
    _cur_epoch->_delegate = EpochTransitionDelegate::RetiringForwardOnly;
    _cur_epoch->_connection_state = EpochConnection::WaitingDisconnect;
    _binding_map.erase(EpochConnection::Current);

    return true;
}

void
ConsensusContainer::EpochStart(uint8_t delegate_idx)
{

    BOOST_LOG(_log) << "ConsensusContainer::EpochStart: " << (int)delegate_idx << " "
                    << (int)NodeIdentityManager::_global_delegate_idx;

    _transition_state = EpochTransitionState::EpochStart;

    if (_transition_delegate == EpochTransitionDelegate::Persistent && !OnNewEpochPostCommit() ||
            _transition_delegate == EpochTransitionDelegate::Retiring && !OnNewEpochReject())
    {
        return;
    }

    _cur_epoch_number++;

    _alarm.add(EPOCH_TRANSITION_END, std::bind(&ConsensusContainer::EpochTransitionEnd, this, delegate_idx));
}

void
ConsensusContainer::EpochTransitionEnd(uint8_t delegate_idx)
{
    std::lock_guard<std::mutex>   lock(_mutex);

    _transition_state = EpochTransitionState::None;

    _trans_epoch = nullptr;

    if (_cur_epoch == nullptr)
    {
        BOOST_LOG(_log) << "ConsensusContainer::EpochTransitionEnd cur epoch is null";
        return;
    }

    if (_transition_delegate == EpochTransitionDelegate::Retiring)
    {
        _binding_map.clear();
        _cur_epoch = nullptr;
    }
    else
    {
        _cur_epoch->_delegate = EpochTransitionDelegate::None;
        _cur_epoch->_connection_state = EpochConnection::Current;
        _binding_map.clear();
        _binding_map[EpochConnection::Current] = _cur_epoch;
    }

    BOOST_LOG(_log) << "ConsensusContainer::EpochTransitionEnd : retired " << (_cur_epoch == nullptr) << " "
                    << (int)delegate_idx << " " << " " << (int)NodeIdentityManager::_global_delegate_idx;

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
