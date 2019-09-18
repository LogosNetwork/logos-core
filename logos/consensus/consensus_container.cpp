/// @file
/// This file contains implementation of the ConsensusContainer class, which encapsulates
/// consensus related types.
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/receive_block.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/request/utility.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>

std::atomic<uint32_t> ConsensusContainer::_cur_epoch_number(0);
const Seconds ConsensusContainer::GARBAGE_COLLECT = Seconds(60);
bool ConsensusContainer::_validate_sig_config = false;

ConsensusContainer::ConsensusContainer(Service & service,
                                       Store & store,
                                       Cache & block_cache,
                                       logos::alarm & alarm,
                                       const logos::node_config & config,
                                       Archiver & archiver,
                                       DelegateIdentityManager & identity_manager,
                                       p2p_interface & p2p)
    : _peer_manager(service, config.consensus_manager_config, *this)
    , _cur_epoch(nullptr)
    , _trans_epoch(nullptr)
    , _service(service)
    , _store(store)
    , _block_cache(block_cache)
    , _alarm(alarm)
    , _config(config)
    , _archiver(archiver)
    , _identity_manager(identity_manager)
    , _transition_state(EpochTransitionState::None)
    , _transition_delegate(EpochTransitionDelegate::None)
    , _p2p(p2p, block_cache)
{
}

void
ConsensusContainer::Start()
{
    for (const ConsensusType & CT : CTs)
    {
        _timer_mutexes[CT];
        _timers.emplace(std::make_pair(CT, Timer(_service)));
        _timer_set[CT] = false;
        _timer_cancelled[CT] = false;
    }
    uint8_t delegate_idx;
    std::shared_ptr<ApprovedEB> approvedEb;
    _identity_manager.CheckAdvertise(_cur_epoch_number, true, delegate_idx, approvedEb);

    _validate_sig_config = _config.tx_acceptor_config.validate_sig &&
            _config.tx_acceptor_config.tx_acceptors.size() == 0; // delegate mode, don't need to re-validate sig

    // is the node a delegate in this epoch
    bool in_epoch = delegate_idx != NON_DELEGATE;

    auto create = [this, approvedEb](const ConsensusManagerConfig &epoch_config) mutable -> void {
        std::lock_guard<std::mutex> lock(_mutex);
        _cur_epoch = CreateEpochManager(_cur_epoch_number, epoch_config, EpochTransitionDelegate::None,
                                        EpochConnection::Current, approvedEb);
        _binding_map[_cur_epoch->_epoch_number] = _cur_epoch;
    };

    /// TODO epoch_transition_enabled is temp to facilitate testing without transition
    if (!DelegateIdentityManager::IsEpochTransitionEnabled())
    {
        create(_config.consensus_manager_config);
    }
    else if (in_epoch)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, *approvedEb);
        create(epoch_config);
    }

    LOG_INFO(_log) << "ConsensusContainer::ConsensusContainer initialized delegate is in epoch "
                    << in_epoch << " epoch transition enabled " << DelegateIdentityManager::IsEpochTransitionEnabled()
                    << " " << (int)delegate_idx << " " << (int)DelegateIdentityManager::GetGlobalDelegateIdx()
                    << " " << _cur_epoch_number;
}

std::shared_ptr<EpochManager>
ConsensusContainer::CreateEpochManager(
    uint epoch_number,
    const ConsensusManagerConfig &config,
    EpochTransitionDelegate delegate,
    EpochConnection connection,
    std::shared_ptr<ApprovedEB> eb)
{
    auto res = std::make_shared<EpochManager>(_service, _store, _block_cache, _alarm, config,
                                              _archiver, _transition_state,
                                              delegate, connection, epoch_number, *this, *this, _p2p._p2p,
                                              config.delegate_id, _peer_manager, eb);
    res->Start();
    return res;
}

logos::process_return
ConsensusContainer::OnDelegateMessage(
    std::shared_ptr<DM> request,
    bool should_buffer)
{
    logos::process_return result;
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnDelegateMessage transaction, the node is not a delegate, "
                       << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
        return result;
    }

    if(!request)
    {
        result.code = logos::process_result::invalid_block_type;
        return result;
    }

    using DM = DelegateMessage<ConsensusType::Request>;

    if(should_buffer)
    {
        result.code = logos::process_result::buffered;
        _cur_epoch->_request_manager->OnBenchmarkDelegateMessage(
            static_pointer_cast<DM>(request), result);
    }
    else
    {
        LOG_DEBUG(_log) << "ConsensusContainer::OnDelegateMessage: "
                        << "RequestType="
                        << GetRequestTypeField(request->type);
        _cur_epoch->_request_manager->OnDelegateMessage(
            static_pointer_cast<DM>(request), result);
    }

    return result;
}

TxChannel::Responses
ConsensusContainer::OnSendRequest(vector<std::shared_ptr<DM>> &blocks)
{
    logos::process_return result;
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest transaction, the node is not a delegate, "
                        << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
        return {{result.code,0}};
    }

    return _cur_epoch->_request_manager->OnSendRequest(blocks);
}

void
ConsensusContainer::AttemptInitiateConsensus(ConsensusType CT)
{
    // Do nothing if we are retired
    OptLock lock(_transition_state, _mutex);

    if (_cur_epoch == nullptr)
    {
        LOG_WARN(_log) << "ConsensusContainer::AttemptInitiateConsensus - the node is not a delegate. global id: "
                       << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
        return;
    }

    switch (CT) {
        case ConsensusType::Request:
            _cur_epoch->_request_manager->OnMessageQueued();
            break;
        case ConsensusType::MicroBlock:
        {
            auto epoch = GetProposerEpoch();
            if (epoch)
            {
                epoch->_micro_manager->OnMessageQueued();
            }
            break;
        }
        case ConsensusType::Epoch:  // highly unlikely that epoch block doesn't complete consensus till next epoch start
            _cur_epoch->_epoch_manager->OnMessageQueued();
            break;
        default:
            LOG_ERROR(_log) << "ConsensusContainer::AttemptInitiateConsensus - invalid consensus type";
    }
}

const std::shared_ptr<EpochManager>
ConsensusContainer::GetProposerEpoch()
{
    auto epoch = _cur_epoch;
    // microblock proposed during epoch transition should be proposed by the new delegate set
    if (_transition_state != EpochTransitionState::None &&
        _cur_epoch->GetConnection() != EpochConnection::Transitioning)
    {
        if (_trans_epoch == nullptr || _trans_epoch->GetConnection() != EpochConnection::Transitioning)
        {
            LOG_WARN(_log) << "ConsensusContainer::GetProposerEpoch - not new delegate set "
                           << (int) DelegateIdentityManager::GetGlobalDelegateIdx();
            return nullptr;
        }
        epoch = _trans_epoch;
    }
    return epoch;
}

void
ConsensusContainer::ScheduleTimer(ConsensusType CT, const TimePoint & timeout)
{
    std::lock_guard<std::mutex> lock(_timer_mutexes[CT]);
    // Do nothing if there's a more imminent timer scheduled
    auto & timer = _timers.at(CT);
    if (timer.expires_at() <= timeout && _timer_set[CT])
    {
        return;
    }
    // should be able to cancel / schedule successfully. remove failure check in production
    auto num_cancelled = timer.expires_at(timeout);
    if (_timer_set[CT] && !num_cancelled)
    {
        LOG_FATAL(_log) << "ConsensusContainer::ScheduleTimer - unexpected timer cancellation for type "
                        << ConsensusToName(CT);
        trace_and_halt();
    }

    timer.async_wait([this, CT](const Error & ec){

        if(ec)
        {
            if (ec == boost::asio::error::operation_aborted)
            {
                LOG_TRACE(_log) << "ConsensusContainer::ScheduleTimer - Timer cancelled for type "
                                << ConsensusToName(CT);
                return;
            }
            LOG_INFO(_log) << "ConsensusContainer::ScheduleTimer - Error for type "
                           << ConsensusToName(CT) << ": " << ec.message();
        }

        {
            std::lock_guard<std::mutex> lock(_timer_mutexes[CT]);
            if (_timer_cancelled[CT])
            {
                LOG_DEBUG(_log) << "ConsensusContainer::ScheduleTimer " << ConsensusToName(CT) << " - forced timer cancellation.";
                assert(!_timer_set[CT]);
                _timer_cancelled[CT] = false;
                return;
            }
            _timer_set[CT] = false;
        }

        AttemptInitiateConsensus(CT);
    });

    // ConsensusManager will cancel the timer right before initiating consensus
    _timer_set[CT] = true;
    LOG_DEBUG(_log) << "ConsensusContainer::ScheduleTimer " << ConsensusToName(CT) << " - scheduled new timer.";
}

void
ConsensusContainer::CancelTimer(ConsensusType CT)
{
    std::lock_guard<std::mutex> lock(_timer_mutexes[CT]);

    auto & timer = _timers.at(CT);
    // Borrowing Devon's design:
    // The below condition is true when the timeout callback
    // has been scheduled and is about to be invoked. In this
    // case, the callback cannot be cancelled, and we have to
    // 'manually' cancel the callback by setting _cancel_timer.
    // When the callback is invoked, it will check this value
    // and return early.
    auto now = Clock::now();
    if(now < timer.expires_at() && !timer.cancel() && _timer_set[CT])
    {
        LOG_DEBUG(_log) << "ConsensusContainer::CancelTimer " << ConsensusToName(CT) << " - force cancel.";
        _timer_cancelled[CT] = true;
    }
    _timer_set[CT] = false;
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
                        << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
        return;
    }

    _cur_epoch->_request_manager->BufferComplete(result);
}

logos::process_return
ConsensusContainer::OnDelegateMessage(
    std::shared_ptr<DelegateMessage<ConsensusType::MicroBlock>> message)
{
    OptLock lock(_transition_state, _mutex);
    logos::process_return result;
    using Request = DelegateMessage<ConsensusType::MicroBlock>;

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnDelegateMessage microblock, the node is not a delegate, "
                        << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
        return result;
    }

    auto epoch = GetProposerEpoch();
    if (!epoch)
    {
        result.code = logos::process_result::old;
        return result;
    }

    message->delegates_epoch_number = epoch->_epoch_number;
    epoch->_micro_manager->OnDelegateMessage(
        std::static_pointer_cast<Request>(message), result);

    return result;
}

logos::process_return
ConsensusContainer::OnDelegateMessage(
    std::shared_ptr<DelegateMessage<ConsensusType::Epoch>> message)
{
    OptLock lock(_transition_state, _mutex);
    logos::process_return result;
    using Request = DelegateMessage<ConsensusType::Epoch>;

    if (_cur_epoch == nullptr)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnDelegateMessage epoch, the node is not a delegate, "
                        << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
        return result;
    }

    message->delegates_epoch_number = _cur_epoch_number;
    _cur_epoch->_epoch_manager->OnDelegateMessage(
        std::static_pointer_cast<Request>(message), result);

    return result;
}

bool
ConsensusContainer::CanBind(uint32_t epoch_number)
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _binding_map.find(epoch_number) != _binding_map.end();
}

bool
ConsensusContainer::Bind(
    std::shared_ptr<Socket> socket,
    const Endpoint endpoint,
    uint32_t epoch_number,
    uint8_t delegate_id)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_cur_epoch == nullptr && _trans_epoch == nullptr)
    {
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder: the node is not accepting connections, "
                        << (int)DelegateIdentityManager::GetGlobalDelegateIdx()
                        << ", delegate = " << unsigned(delegate_id)
                        << ", epoch_number = " << epoch_number;
        return false;
    }

    if (_binding_map.find(epoch_number) == _binding_map.end())
    {
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder epoch manager is not available for "
                        << " delegate =" << (int)delegate_id
                        << " epoch =" << epoch_number;
        return false;
    }

    auto epoch = _binding_map[epoch_number];

    LOG_INFO(_log) << "ConsensusContainer::PeerBinder, binding connection "
                    << epoch->GetConnectionName()
                    << " delegate " << epoch->GetDelegateName()
                    << " state " << epoch->GetStateName()
                    << " " << (int)DelegateIdentityManager::GetGlobalDelegateIdx()
                    << ", delegate_id = " << unsigned(delegate_id)
                    << ", epoch_number = " << epoch_number;

    epoch->_netio_manager->OnConnectionAccepted(endpoint, socket, delegate_id);

    return true;
}

void
ConsensusContainer::EpochTransitionEventsStart()
{
    std::lock_guard<std::mutex> lock(_mutex);
    uint8_t delegate_idx;
    std::shared_ptr<ApprovedEB> approvedEb;

    if (!DelegateIdentityManager::IsEpochTransitionEnabled())
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochTransitionEventsStart "
                           "epoch transition is not supported by this delegate "
                        << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
        return;
    }

    _identity_manager.IdentifyDelegates(EpochDelegates::Next, delegate_idx, approvedEb);

    if (delegate_idx == NON_DELEGATE && _cur_epoch == nullptr)
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochTransitionEventsStart not a delegate in current or next epoch, "
                        << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
        return;
    }

    if (delegate_idx == NON_DELEGATE)
    {
        _transition_delegate = EpochTransitionDelegate::Retiring;
    }
    else if (_cur_epoch == nullptr)
    {
        // update epoch number
        ApprovedEB eb;
        Tip tip;
        BlockHash &hash = tip.digest;
        if (_store.epoch_tip_get(tip))
        {
            LOG_FATAL(_log) << "ConsensusContainer::EpochTransitionEventsStart failed to get epoch tip";
            trace_and_halt();
        }

        if (_store.epoch_get(hash, eb))
        {
            LOG_FATAL(_log) << "ConsensusContainer::EpochTransitionEventsStart failed to get epoch "
                            << hash.to_string();
            trace_and_halt();
        }
        _cur_epoch_number = eb.epoch_number + 1;

        _transition_delegate = EpochTransitionDelegate::New;
    }
    else
    {
        _transition_delegate = EpochTransitionDelegate::Persistent;
    }

    _transition_state = EpochTransitionState::Connecting;

    if (_transition_delegate != EpochTransitionDelegate::Retiring)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, *approvedEb);
        _trans_epoch = CreateEpochManager(_cur_epoch_number+1, epoch_config, _transition_delegate,
                                          EpochConnection::Transitioning, approvedEb);

        if (_transition_delegate == EpochTransitionDelegate::Persistent)
        {
            CheckEpochNull(!_cur_epoch, "EpochTransitionEventsStart");
            _cur_epoch->_delegate = EpochTransitionDelegate::Persistent;
        }
        LOG_INFO(_log) << "ConsensusContainer::EpochTransitionEventsStart"
            << " - binding epoch manager for epoch_number = " 
            << _trans_epoch->_epoch_number;

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

    LOG_INFO(_log) << "ConsensusContainer::EpochTransitionEventsStart : delegate "
                   << TransitionStateToName(_transition_state) << " "
                   << TransitionDelegateToName(_transition_delegate) <<  " "
                   << (int)delegate_idx << " " << (int)DelegateIdentityManager::GetGlobalDelegateIdx()
                   << ", epoch " << _cur_epoch_number
                   << ", binding map " << ((_trans_epoch) ? _trans_epoch->_epoch_number : 0);

    _alarm.add(lapse, std::bind(&ConsensusContainer::EpochTransitionStart, this, delegate_idx));
}

void
ConsensusContainer::EpochTransitionStart(uint8_t delegate_idx)
{
    std::lock_guard<std::mutex>   lock(_mutex);
    LOG_INFO(_log) << "ConsensusContainer::EpochTransitionStart "
                    << TransitionDelegateToName(_transition_delegate) <<  " "
                    << (int)delegate_idx
                    << " " << (int)DelegateIdentityManager::GetGlobalDelegateIdx()
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

    CheckEpochNull(!_cur_epoch && !_trans_epoch, "OnPostCommit");

    // only transition if it hasn't happened already (avoiding transitioning twice in race conditions)
    if (_trans_epoch && _trans_epoch->_epoch_number == epoch_number)
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
        _trans_epoch->_delegate = EpochTransitionDelegate::PersistentReject;
        _trans_epoch->_connection_state = EpochConnection::WaitingDisconnect;
    }
}

void
ConsensusContainer::TransitionDelegate(EpochTransitionDelegate delegate)
{
    if (delegate == EpochTransitionDelegate::Retiring)
    {
        if (!_cur_epoch)
        {
            // We may have already called TransitionRetiring once (from OnPrePrepareRejected)
            CheckEpochNull(!_trans_epoch || _trans_epoch->_delegate != EpochTransitionDelegate::RetiringForwardOnly,
                    "TransitionDelegateRetiring");
            return;
        }
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
                    << (int)DelegateIdentityManager::GetGlobalDelegateIdx() << " " << (_cur_epoch_number + 1);

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
                   << " " << (int)delegate_idx << " " << (int)DelegateIdentityManager::GetGlobalDelegateIdx()
                   << " " << _cur_epoch_number;

    _transition_state = EpochTransitionState::None;

    if (_transition_delegate != EpochTransitionDelegate::New)
    {
        _trans_epoch->CleanUp();
    }

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
    const ApprovedEB & epoch)
{
   ConsensusManagerConfig config = _config.consensus_manager_config;

   config.delegate_id = delegate_idx;
   config.local_address = _config.consensus_manager_config.local_address;
   config.delegates.clear();

   stringstream str;
   str << "ConsensusContainer::BuildConsensusConfig: ";
   for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
   {
        auto account = epoch.delegates[del].account;
        auto ip = _identity_manager.GetDelegateIP(
                DelegateIdentityManager::CurFromDelegatesEpoch(epoch.epoch_number), del);
        if (ip != "")
        {
            config.delegates.push_back(ConsensusManagerConfig::Delegate{ip, del});
        }
        str << (int)del << " " << ((ip != "")?ip:"-")<< " ";
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

std::shared_ptr<Request> Deserialize(uint8_t* data, uint32_t size)
{
    bool error = false;
    logos::bufferstream stream(data,size);
    std::shared_ptr<Request> req = DeserializeRequest(error,stream);
    if(error)
    {
        Log log;
        LOG_WARN(log) << "ConsensusContainer - Deserialize - "
            << "error deserializing request from p2p network";
        return nullptr;
    }
    return req;
}

bool
ConsensusContainer::OnP2pReceive(const void *data, size_t size)
{
    bool error = false;
    logos::bufferstream stream((const uint8_t*)data, P2pHeader::SIZE);
    P2pHeader p2pheader(error, stream);
    if (error)
    {
        LOG_ERROR(_log) << "ConsensusContainer::OnP2pReceive, failed to deserialize P2pHeader";
        return false;
    }

    LOG_DEBUG(_log) << "ConsensusContainer::OnP2pReceive, received p2p message "
                    << p2pheader.app_type
                    << ", size " << size;

    data = (uint8_t*)data + P2pHeader::SIZE;
    size -= P2pHeader::SIZE;

    switch (p2pheader.app_type)
    {
        case P2pAppType::Consensus: {
            return OnP2pConsensus((uint8_t*)data, size);
        }
        case P2pAppType::AddressAd: {
            return OnAddressAd((uint8_t*)data, size);
        }
        case P2pAppType::AddressAdTxAcceptor: {
            return OnAddressAdTxAcceptor((uint8_t*)data, size);
        }
        case P2pAppType::Request: {
            std::shared_ptr<Request> request = Deserialize((uint8_t*)data, size);
            if(!request)
            {
                LOG_ERROR(_log) << "ConsensusContainer::OnP2pReceive-"
                    << "error deserializing request";
                return false;
            }

            LOG_DEBUG(_log) << "ConsensusContainer::OnP2pReceive-Request"
                << ",hash=" << request->Hash().to_string();
                 

            //if the Request already exists in the store, do not propagate            
            if(_store.request_exists(request->Hash()))
            {
                LOG_DEBUG(_log) << "P2PRequestPropagation-"
                    << "hash=" << request->Hash().to_string()
                    << ",request_exists,not propagating";
                return false;
            }

            logos::process_return result = 
                OnDelegateMessage(
                        static_pointer_cast<DelegateMessage<ConsensusType::Request>>(request),false);

            LOG_DEBUG(_log) << "ConsensusContainer::OnP2pReceive-Request"
                << ",hash=" << request->Hash().to_string()
                << ",result=" << ProcessResultToString(result.code);
            //if not a delegate, propagate if valid
            if(result.code == logos::process_result::not_delegate)
            {
                bool res = _block_cache.ValidateRequest(
                            request,ConsensusContainer::GetCurEpochNumber(),result);
                if(res)
                {
                    LOG_DEBUG(_log) << "P2PRequestPropagation-"
                        << "hash=" << request->Hash().to_string()
                        << ",non_delegate"
                        << ",propagating";
                }
                else
                {
                   LOG_DEBUG(_log) << "P2PRequestPropagation-"
                        << "hash=" << request->Hash().to_string()
                        << ",non_delegate"
                        << ",request invalid,not propagating"
                        << ",result=" << ProcessResultToString(result.code);
                
                }
                return res;
            }
            else
            {

                if(result.code == logos::process_result::progress
                        || result.code == logos::process_result::pending)
                {
                    LOG_DEBUG(_log) << "P2PRequestPropagation-"
                        << "hash=" << request->Hash().to_string()
                        << ",delegate"
                        << ",processing,propagating"
                        << ",result=" << ProcessResultToString(result.code);
                    //If we are processing the Request, still propagate to p2p
                    //Propagating the Request via p2p adds the Request to the
                    //p2p propagate store, which prevents deserializing this
                    //request more than once (due to multiple peers propagating
                    //this request)
                    return true;
                }
                else
                {
                    LOG_DEBUG(_log) << "P2PRequestPropagation-"
                        << "hash=" << request->Hash().to_string()
                        << ",delegate"
                        << ",request invalid,not propagating"
                        << ",result=" << ProcessResultToString(result.code);
                    //if the Request is invalid, do not propagate 
                    return false;
                }
            }
        }
        default:
            return false;
    }
}

bool
ConsensusContainer::OnP2pConsensus(uint8_t *data, size_t size)
{
    auto hdrs_size = P2pConsensusHeader::SIZE + MessagePrequelSize;
    bool error = false;
    logos::bufferstream stream(data, size);
    P2pConsensusHeader p2pconsensus_header(error, stream);
    if (error)
    {
        LOG_ERROR(_log) << "ConsensusContainer::OnP2pReceive, failed to deserialize P2pConsensusHeader";
        return false;
    }
    error = false;
    Prequel prequel(error, stream);
    if (error)
    {
        LOG_ERROR(_log) << "ConsensusContainer::OnP2pReceive, failed to deserialize Prequel";
        return false;
    }

    if (size != (hdrs_size + prequel.payload_size))
    {
        LOG_ERROR(_log) << "ConsensusContainer::OnP2pReceive, invalid message size, " << size
                        << " payload size " << prequel.payload_size;
        return false;
    }

    size -= hdrs_size;
    uint8_t *payload_data = data + hdrs_size;
    if (prequel.type == MessageType::Post_Committed_Block)
    {
        LOG_DEBUG(_log) << "ConsensusContainer::OnP2pReceive, processing post committed block, size "
                        << size;
        return _p2p.ProcessInputMessage(prequel, payload_data, size);
    }

    std::shared_ptr<EpochManager> epoch = nullptr;

    {
        OptLock lock(_transition_state, _mutex);

        if (_cur_epoch && _cur_epoch->GetEpochNumber() == p2pconsensus_header.epoch_number) {
            epoch = _cur_epoch;
        } else if (_trans_epoch && _trans_epoch->GetEpochNumber() == p2pconsensus_header.epoch_number) {
            epoch = _trans_epoch;
        }
    }

    if (epoch && (p2pconsensus_header.dest_delegate_id == 0xff ||
                  p2pconsensus_header.dest_delegate_id == epoch->GetDelegateId()))
    {
        LOG_DEBUG(_log) << "ConsensusContainer::OnP2pReceive, adding to consensus queue "
                        << MessageToName(prequel.type) << " " << ConsensusToName(prequel.consensus_type)
                        << " payload size " << prequel.payload_size
                        << " src delegate " << (int)p2pconsensus_header.src_delegate_id
                        << " dest delegate " << (int)p2pconsensus_header.dest_delegate_id;

        return epoch->_netio_manager->AddToConsensusQueue(payload_data, prequel.version,
                                                          prequel.type, prequel.consensus_type,
                                                          prequel.payload_size, p2pconsensus_header.src_delegate_id);
    }
    else
    {
        LOG_WARN(_log) << "ConsensusContainer::OnP2pReceive, no matching epoch or delegate id "
                       << ", epoch " << p2pconsensus_header.epoch_number
                       << ", delegate id " << (int)p2pconsensus_header.dest_delegate_id;
    }

    return true;
}

std::shared_ptr<EpochManager>
ConsensusContainer::GetEpochManager(uint32_t epoch_number)
{
    OptLock lock(_transition_state, _mutex);

    std::shared_ptr<EpochManager> epoch = nullptr;
    if (_cur_epoch && _cur_epoch->GetEpochNumber() == epoch_number)
    {
        epoch = _cur_epoch;
    }
    else if (_trans_epoch && _trans_epoch->GetEpochNumber() == epoch_number)
    {
        epoch = _trans_epoch;
    }

    return epoch;
}

bool
ConsensusContainer::OnAddressAd(uint8_t *data, size_t size)
{
    bool error = false;
    logos::bufferstream stream(data, PrequelAddressAd::SIZE);
    PrequelAddressAd prequel(error, stream);
    if (error)
    {
        LOG_DEBUG(_log) << "ConsensusContainer::OnAddressAd, failed to deserialize PrequelAddressAd";
        return false;
    }

    auto epoch = GetEpochManager(prequel.epoch_number);

    LOG_DEBUG(_log) << "ConsensusContainer::OnAddressAd epoch " << prequel.epoch_number
                    << " delegate id " << (int)prequel.delegate_id
                    << " encr delegate id " << (int)prequel.encr_delegate_id
                    << " from epoch delegate id " << ((epoch!=0)?(int)epoch->_delegate_id:255)
                    << " size " << size;

    std::string ip = "";
    uint16_t port = 0;

    if (_identity_manager.OnAddressAd(data, size, prequel, ip, port) && epoch)
    {
        epoch->_netio_manager->AddDelegate(prequel.delegate_id, ip, port);
    }

    return true;
}

bool
ConsensusContainer::OnAddressAdTxAcceptor(uint8_t *data, size_t size)
{
    return _identity_manager.OnAddressAdTxAcceptor(data, size);
}
