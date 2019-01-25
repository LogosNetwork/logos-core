/// @file
/// This file contains implementation of the ConsensusContainer class, which encapsulates
/// consensus related types.
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/receive_block.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>

std::atomic<uint32_t> ConsensusContainer::_cur_epoch_number(0);
const Seconds ConsensusContainer::GARBAGE_COLLECT = Seconds(60);
bool ConsensusContainer::_validate_sig_config = false;

ConsensusContainer::ConsensusContainer(Service & service,
                                       Store & store,
                                       logos::alarm & alarm,
                                       const logos::node_config & config,
                                       Archiver & archiver,
                                       DelegateIdentityManager & identity_manager,
                                       p2p_interface & p2p)
    : _peer_manager(service, config.consensus_manager_config, std::bind(&ConsensusContainer::PeerBinder, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
    , _cur_epoch(nullptr)
    , _trans_epoch(nullptr)
    , _service(service)
    , _store(store)
    , _alarm(alarm)
    , _config(config.consensus_manager_config)
    , _archiver(archiver)
    , _identity_manager(identity_manager)
    , _transition_state(EpochTransitionState::None)
    , _transition_delegate(EpochTransitionDelegate::None)
    , _p2p(p2p, store)
{
    uint8_t delegate_idx;
    Accounts delegates;
    _identity_manager.IdentifyDelegates(EpochDelegates::Current, delegate_idx, delegates);

    _validate_sig_config = config.tx_acceptor_config.validate_sig &&
            config.tx_acceptor_config.tx_acceptors.size() == 0; // delegate mode, don't need to re-validate sig

    // is the node a delegate in this epoch
    bool in_epoch = delegate_idx != NON_DELEGATE;

    auto create = [this](const ConsensusManagerConfig &epoch_config) mutable -> void {
        std::lock_guard<std::mutex> lock(_mutex);
        _cur_epoch = CreateEpochManager(_cur_epoch_number, epoch_config, EpochTransitionDelegate::None,
                                        EpochConnection::Current);
        _binding_map[_cur_epoch->_epoch_number] = _cur_epoch;
    };

    /// TODO epoch_transition_enabled is temp to facilitate testing without transition
    if (!DelegateIdentityManager::IsEpochTransitionEnabled())
    {
        create(_config);
    }
    else if (in_epoch)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, delegates);
        create(epoch_config);
    }

    LOG_INFO(_log) << "ConsensusContainer::ConsensusContainer initialized delegate is in epoch "
                    << in_epoch << " epoch transition enabled " << DelegateIdentityManager::IsEpochTransitionEnabled()
                    << " " << (int)delegate_idx << " " << (int)DelegateIdentityManager::_global_delegate_idx
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
                                          delegate, connection, epoch_number, *this, _p2p._p2p);
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<Send> block,
    bool should_buffer)
{
    logos::process_return result;
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest transaction, the node is not a delegate, "
                        << (int)DelegateIdentityManager::_global_delegate_idx;
        return result;
    }

	if(!block)
	{
	    result.code = logos::process_result::invalid_block_type;
	    return result;
	}

    if(should_buffer)
    {
        result.code = logos::process_result::buffered;
        _cur_epoch->_batch_manager.OnBenchmarkSendRequest(block, result);
    }
    else
    {
        LOG_DEBUG(_log) << "ConsensusContainer::OnSendRequest: "
                        << "number_transaction=" << block->transactions.size();
        _cur_epoch->_batch_manager.OnSendRequest(
            static_pointer_cast<Request>(block), result);
    }

    return result;
}

TxChannel::Responses
ConsensusContainer::OnSendRequest(vector<std::shared_ptr<Request>> &blocks)
{
    logos::process_return result;
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest transaction, the node is not a delegate, "
                        << (int)DelegateIdentityManager::_global_delegate_idx;
        return {{result.code,0}};
    }

    return _cur_epoch->_batch_manager.OnSendRequest(blocks);
}

void
ConsensusContainer::BufferComplete(
    logos::process_return & result)
{
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::BufferComplete transaction, the node is not a delegate, "
                        << (int)DelegateIdentityManager::_global_delegate_idx;
        return;
    }

    _cur_epoch->_batch_manager.BufferComplete(result);
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<RequestMessage<ConsensusType::MicroBlock>> block)
{
    OptLock lock(_transition_state, _mutex);
    logos::process_return result;
	using Request = RequestMessage<ConsensusType::MicroBlock>;

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest microblock, the node is not a delegate, "
                        << (int)DelegateIdentityManager::_global_delegate_idx;
        return result;
    }

    block->primary_delegate = _cur_epoch->_epoch_manager.GetDelegateIndex();
    _cur_epoch->_micro_manager.OnSendRequest(
        std::static_pointer_cast<Request>(block), result);;

    return result;
}

logos::process_return
ConsensusContainer::OnSendRequest(
    std::shared_ptr<RequestMessage<ConsensusType::Epoch>> block)
{
    OptLock lock(_transition_state, _mutex);
    logos::process_return result;
    using Request = RequestMessage<ConsensusType::Epoch>;

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest epoch, the node is not a delegate, "
                        << (int)DelegateIdentityManager::_global_delegate_idx;
        return result;
    }

    block->primary_delegate = _cur_epoch->_epoch_manager.GetDelegateIndex();
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
                        << (int)DelegateIdentityManager::_global_delegate_idx;
        return;
    }

    // due to the clock drift another delegate is already in the epoch transition state,
    // while this delegate has not received yet the epoch transition event - queue up the connection
    // for binding to the right instance of EpochManager once the epoch transition event is received
    if (ids.connection == EpochConnection::Transitioning && _transition_state == EpochTransitionState::None)
    {
        _connections_queue.push(ConnectionCache{socket, ids, endpoint});
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder: epoch transition has not started yet, "
                        << (int)DelegateIdentityManager::_global_delegate_idx;
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
                    << " " << (int)DelegateIdentityManager::_global_delegate_idx;

    epoch->_netio_manager.OnConnectionAccepted(endpoint, socket, ids);
}

void
ConsensusContainer::EpochTransitionEventsStart()
{
    std::lock_guard<std::mutex> lock(_mutex);
    uint8_t delegate_idx;
    Accounts delegates;

    if (!DelegateIdentityManager::IsEpochTransitionEnabled())
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochTransitionEventsStart "
                           "epoch transition is not supported by this delegate "
                        << (int)DelegateIdentityManager::_global_delegate_idx;
        return;
    }

    _identity_manager.IdentifyDelegates(EpochDelegates::Next, delegate_idx, delegates);

    if (delegate_idx == NON_DELEGATE && _cur_epoch == nullptr)
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochTransitionEventsStart not a delegate in current or next epoch, "
                        << (int)DelegateIdentityManager::_global_delegate_idx;
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
                    << (int)delegate_idx << " " << (int)DelegateIdentityManager::_global_delegate_idx
                    << " " << _cur_epoch_number;

    if (_transition_delegate != EpochTransitionDelegate::Retiring)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, delegates);
        _trans_epoch = CreateEpochManager(_cur_epoch_number+1, epoch_config, _transition_delegate,
                                          EpochConnection::Transitioning);

        if (_transition_delegate == EpochTransitionDelegate::Persistent)
        {
            CheckEpochNull(!_cur_epoch, "EpochTransitionEventsStart");
            _cur_epoch->_delegate = EpochTransitionDelegate::Persistent;
        }

        // New and Persistent delegates in the new delegate's set
        _binding_map[_trans_epoch->_epoch_number] = _trans_epoch;
    }
    else
    {
        _cur_epoch->_delegate = EpochTransitionDelegate::Retiring;
    }

    // TODO recall may have different timers
    Milliseconds lapse = EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START;
    EpochTimeUtil util;
    Milliseconds epoch_start = util.GetNextEpochTime();
    if (epoch_start > EPOCH_TRANSITION_START && epoch_start < EPOCH_DELEGATES_CONNECT)
    {
        lapse = epoch_start - EPOCH_TRANSITION_START;
    }
    else if (epoch_start < EPOCH_TRANSITION_START)
    {
        lapse = Milliseconds(0);
    }

    _alarm.add(lapse, std::bind(&ConsensusContainer::EpochTransitionStart, this, delegate_idx));

    BindConnectionsQueue();
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
                    << " " << (int)DelegateIdentityManager::_global_delegate_idx
                    << " " << _cur_epoch_number;

    _transition_state = EpochTransitionState::EpochTransitionStart;

    if (_transition_delegate == EpochTransitionDelegate::New)
    {
        CheckEpochNull(!_trans_epoch, "EpochTransitionStart");
        _cur_epoch = _trans_epoch;
        _trans_epoch = nullptr;
    }

    EpochTimeUtil util;
    auto epoch_start = util.GetNextEpochTime();
    auto lapse = epoch_start < EPOCH_START ? epoch_start : EPOCH_START;

    _alarm.add(lapse, std::bind(&ConsensusContainer::EpochStart, this, delegate_idx));
}

void
ConsensusContainer::OnPostCommit(uint32_t epoch_number)
{
    std::lock_guard<std::mutex>   lock(_mutex);

    CheckEpochNull(!_cur_epoch || !_trans_epoch, "OnPostCommit");

    if (_trans_epoch->_epoch_number == epoch_number)
    {
        TransitionPersistent();
    }
}

void
ConsensusContainer::TransitionPersistent()
{
    if (_trans_epoch->_delegate != EpochTransitionDelegate::PersistentReject)
    {
        _cur_epoch.swap(_trans_epoch);
        _cur_epoch->UpdateRequestPromoter();
        _trans_epoch->_delegate = EpochTransitionDelegate::PersistentReject;
        _trans_epoch->_connection_state = EpochConnection::WaitingDisconnect;
    }
}

void
ConsensusContainer::TransitionDelegate(EpochTransitionDelegate delegate)
{
    if (delegate == EpochTransitionDelegate::Retiring)
    {
        CheckEpochNull(!_cur_epoch, "TransitionDelegate Retiring");
        TransitionRetiring();
    }
    else if (delegate == EpochTransitionDelegate::Persistent)
    {
        CheckEpochNull(!_cur_epoch || !_trans_epoch, "TransitionDelegate Persistent");
        TransitionPersistent();
    }
}

void
ConsensusContainer::OnPrePrepareRejected(EpochTransitionDelegate delegate)
{
    std::lock_guard<std::mutex>   lock(_mutex);
    TransitionDelegate(delegate);
}

void
ConsensusContainer::TransitionRetiring()
{
    _cur_epoch->_delegate = EpochTransitionDelegate::RetiringForwardOnly;
    _cur_epoch->_connection_state = EpochConnection::WaitingDisconnect;

    // stop receiving requests
    _trans_epoch.swap(_cur_epoch);
    _cur_epoch = nullptr;
}

void
ConsensusContainer::EpochStart(uint8_t delegate_idx)
{
    std::lock_guard<std::mutex>   lock(_mutex);

    LOG_INFO(_log) << "ConsensusContainer::EpochStart "
                    << TransitionDelegateToName(_transition_delegate) <<  " "
                    << (int)delegate_idx << " "
                    << (int)DelegateIdentityManager::_global_delegate_idx << " " << (_cur_epoch_number + 1);

    _transition_state = EpochTransitionState::EpochStart;

    TransitionDelegate(_transition_delegate);

    _binding_map.erase(_cur_epoch_number);

    _cur_epoch_number++;

    _alarm.add(EPOCH_TRANSITION_END, std::bind(&ConsensusContainer::EpochTransitionEnd, this, delegate_idx));
}

void
ConsensusContainer::EpochTransitionEnd(uint8_t delegate_idx)
{
    std::lock_guard<std::mutex>   lock(_mutex);

    LOG_INFO(_log) << "ConsensusContainer::EpochTransitionEnd "
                   << TransitionDelegateToName(_transition_delegate)
                   << " " << (int)delegate_idx << " " << (int)DelegateIdentityManager::_global_delegate_idx
                   << " " << _cur_epoch_number;

    _transition_state = EpochTransitionState::None;

    if (_transition_delegate != EpochTransitionDelegate::New)
    {
        _trans_epoch->CleanUp();
    }

    // schedule for destruction
    auto gb = _trans_epoch;
    _alarm.add(GARBAGE_COLLECT, [gb]() mutable -> void {
        gb = nullptr;
    });
    _trans_epoch = nullptr;

    if (_transition_delegate == EpochTransitionDelegate::Retiring)
    {
        _binding_map.clear();
    }
    else
    {
        CheckEpochNull(!_cur_epoch, "EpochTransitionEnd");
        _cur_epoch->_delegate = EpochTransitionDelegate::None;
        _cur_epoch->_connection_state = EpochConnection::Current;
    }

    _transition_delegate = EpochTransitionDelegate::None;
}

ConsensusManagerConfig
ConsensusContainer::BuildConsensusConfig(
    const uint8_t delegate_idx,
    const Accounts & delegates)
{
   ConsensusManagerConfig config = _config;

   config.delegate_id = delegate_idx;
   config.local_address = DelegateIdentityManager::_delegates_ip[DelegateIdentityManager::_delegate_account];
   config.delegates.clear();

   stringstream str;
   str << "ConsensusContainer::BuildConsensusConfig: ";
   for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
   {
        auto account = delegates[del];
        auto ip = DelegateIdentityManager::_delegates_ip[account];
        config.delegates.push_back(ConsensusManagerConfig::Delegate{ip, del});
        str << (int)del << " " << ip << " ";
   }
   LOG_DEBUG(_log) << str.str();

   return config;
}

bool
ConsensusContainer::IsRecall()
{
    return _archiver.IsRecall();
}

void
ConsensusContainer::CheckEpochNull(bool is_null, const char* where)
{
    if (is_null)
    {
        LOG_FATAL(_log) << "ConsensusContainer::" << where
                        << " cur null " << !_cur_epoch << " trans null " << !_trans_epoch;
        trace_and_halt();
    }
}

bool
ConsensusContainer::OnP2pReceive(const void *message, size_t size) {
    return _p2p.ProcessInputMessage(message, size);
}
