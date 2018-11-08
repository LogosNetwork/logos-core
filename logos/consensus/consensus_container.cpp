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
    , _archiver(archiver)
    , _identity_manager(identity_manager)
    , _transition_state(EpochTransitionState::None)
    , _transition_delegate(EpochTransitionDelegate::None)
    , _epoch_transition_enabled(true)
{
    // Currently require that all_delegates is twice the size of delegates
    if (config.all_delegates.size() != 2 * config.delegates.size())
    {
        _epoch_transition_enabled = false;
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
        _binding_map[_cur_epoch->_epoch_number] = _cur_epoch;
    };

    if (!_epoch_transition_enabled)
    {
        create(config);
    }
    else if (in_epoch)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, delegates);
        create(epoch_config);
    }

    LOG_INFO(_log) << "ConsensusContainer::ConsensusContainer initialized delegate is in epoch "
                    << in_epoch << " epoch transition enabled " << _epoch_transition_enabled
                    << " " << (int)delegate_idx << " " << (int)NodeIdentityManager::_global_delegate_idx
                    << " " << _cur_epoch_number;
}

std::shared_ptr<EpochManager>
ConsensusContainer::CreateEpochManager(
    uint epoch_number,
    const ConsensusManagerConfig &config,
    EpochTransitionDelegate delegate,
    EpochConnection connection )
{
    return std::make_shared<EpochManager>(_service, _store, _alarm, config,
                                          _archiver, _peer_manager, _transition_state,
                                          delegate, connection, epoch_number, *this);
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
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest transaction, the node is not a delegate, "
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
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest transaction, the node is not a delegate, "
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
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest microblock, the node is not a delegate, "
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
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest epoch, the node is not a delegate, "
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
    ConnectedClientIds ids)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_cur_epoch == nullptr && _trans_epoch == nullptr)
    {
        socket->close();
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder: the node is not accepting connections, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return;
    }

    // due to the clock drift another delegate is already in the epoch transition state,
    // while this delegate has not received yet the epoch transition event - queue up the connection
    // for binding to the right instance of EpochManager once the epoch transition event is received
    if (ids.connection == EpochConnection::Transitioning && _transition_state == EpochTransitionState::None)
    {
        _connections_queue.push(ConnectionCache{socket, ids, endpoint});
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder: epoch transition has not started yet, "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return;
    }

    if (ids.connection == EpochConnection::WaitingDisconnect)
    {
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder will not connect retiring delegate";
        return;
    }

    if (_binding_map.find(ids.epoch_number) == _binding_map.end())
    {
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder epoch manager is not available for "
                        << " delegate " << (int)ids.delegate_id << " connection "
                        << TransitionConnectionToName(ids.connection)
                        << " epoch " << ids.epoch_number;
        return;
    }

    auto epoch = _binding_map[ids.epoch_number];

    LOG_INFO(_log) << "ConsensusContainer::PeerBinder, binding connection "
                    << TransitionConnectionToName(ids.connection) << " to "
                    << epoch->GetConnectionName()
                    << " delegate " << epoch->GetDelegateName()
                    << " state " << epoch->GetStateName()
                    << " " << (int)NodeIdentityManager::_global_delegate_idx;

    epoch->_netio_manager.OnConnectionAccepted(endpoint, socket, ids);
}

void
ConsensusContainer::EpochTransitionEventsStart()
{
    std::lock_guard<std::mutex> lock(_mutex);
    uint8_t delegate_idx;
    Accounts delegates;

    if (!_epoch_transition_enabled)
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochTransitionEventsStart "
                           "epoch transition is not supported by this delegate "
                        << (int)NodeIdentityManager::_global_delegate_idx;
        return;
    }

    _identity_manager.IdentifyDelegates(EpochDelegates::Next, delegate_idx, delegates);

    if (delegate_idx == NON_DELEGATE && _cur_epoch == nullptr)
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochTransitionEventsStart not a delegate in current or next epoch, "
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

    LOG_INFO(_log) << "ConsensusContainer::EpochTransitionEventsStart : delegate "
                    << TransitionStateToName(_transition_state) << " "
                    << TransitionDelegateToName(_transition_delegate) <<  " "
                    << (int)delegate_idx << " " << (int)NodeIdentityManager::_global_delegate_idx
                    << " " << _cur_epoch_number;

    if (_transition_delegate != EpochTransitionDelegate::Retiring)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, delegates);
        _trans_epoch = CreateEpochManager(_cur_epoch_number+1, epoch_config, _transition_delegate,
                                          EpochConnection::Transitioning);

        if (_transition_delegate == EpochTransitionDelegate::Persistent)
        {
            if (_cur_epoch == nullptr)
            {
                LOG_ERROR(_log) << "ConsensusContainer::EpochTransitionEventsStart current epoch is null";
                return;
            }
            _cur_epoch->_delegate = EpochTransitionDelegate::Persistent;
        }

        // New and Persistent delegates in the new delegate's set
        _binding_map[_trans_epoch->_epoch_number] = _trans_epoch;
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
            PeerBinder(element.endpoint, element.socket, element.ids);
        });
    }
}

void
ConsensusContainer::EpochTransitionStart(uint8_t delegate_idx)
{
    std::lock_guard<std::mutex>   lock(_mutex);
    LOG_INFO(_log) << "ConsensusContainer::EpochTransitionStart "
                    << TransitionDelegateToName(_transition_delegate) <<  " "
                    << (int)delegate_idx
                    << " " << (int)NodeIdentityManager::_global_delegate_idx
                    << " " << _cur_epoch_number;

    _transition_state = EpochTransitionState::EpochTransitionStart;

    if (_transition_delegate == EpochTransitionDelegate::New)
    {
        if (_trans_epoch == nullptr || _cur_epoch != nullptr)
        {
           LOG_WARN(_log) << "ConsensusContainer::EpochTransitionStart trans epoch is null "
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
        LOG_ERROR(_log) << "ConsensusContainer::PersistentToReject trans epoch is null "
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

    return true;
}

bool
ConsensusContainer::OnNewEpochRejected()
{
    std::lock_guard<std::mutex>   lock(_mutex);

    if (_cur_epoch == nullptr)
    {
        LOG_ERROR(_log) << "ConsensusContainer::RetiringToForwardOnly cur epoch is null";
        return false;
    }
    _cur_epoch->_delegate = EpochTransitionDelegate::RetiringForwardOnly;
    _cur_epoch->_connection_state = EpochConnection::WaitingDisconnect;

    // stop receiving requests
    _trans_epoch.swap(_cur_epoch);
    _cur_epoch = nullptr;

    return true;
}

void
ConsensusContainer::EpochStart(uint8_t delegate_idx)
{

    LOG_INFO(_log) << "ConsensusContainer::EpochStart "
                    << TransitionDelegateToName(_transition_delegate) <<  " "
                    << (int)delegate_idx << " "
                    << (int)NodeIdentityManager::_global_delegate_idx << " " << (_cur_epoch_number + 1);

    _transition_state = EpochTransitionState::EpochStart;

    if (_transition_delegate == EpochTransitionDelegate::Persistent && !OnNewEpochPostCommit() ||
            _transition_delegate == EpochTransitionDelegate::Retiring && !OnNewEpochRejected())
    {
        return;
    }

    _binding_map.erase(_cur_epoch_number);

    _cur_epoch_number++;

    _alarm.add(EPOCH_TRANSITION_END, std::bind(&ConsensusContainer::EpochTransitionEnd, this, delegate_idx));
}

void
ConsensusContainer::EpochTransitionEnd(uint8_t delegate_idx)
{
    std::lock_guard<std::mutex>   lock(_mutex);

    _transition_state = EpochTransitionState::None;

    _trans_epoch = nullptr;


    if (_transition_delegate == EpochTransitionDelegate::Retiring)
    {
        _binding_map.clear();
        _trans_epoch = nullptr;
    }
    else if (_cur_epoch == nullptr)
    {
        LOG_ERROR(_log) << "ConsensusContainer::EpochTransitionEnd cur epoch is null";
        return;
    }
    else
    {
        _cur_epoch->_delegate = EpochTransitionDelegate::None;
        _cur_epoch->_connection_state = EpochConnection::Current;
    }

    LOG_INFO(_log) << "ConsensusContainer::EpochTransitionEnd "
                    << TransitionDelegateToName(_transition_delegate)
                    << " " << (int)delegate_idx << " " << (int)NodeIdentityManager::_global_delegate_idx
                    << " " << _cur_epoch_number;

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
