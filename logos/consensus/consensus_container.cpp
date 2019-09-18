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
bool ConsensusContainer::_validate_sig_config = false;

ConsensusContainer::ConsensusContainer(Service & service,
                                       Store & store,
                                       Cache & block_cache,
                                       logos::alarm & alarm,
                                       const logos::node_config & config,
                                       IRecallHandler & recall_handler,
                                       DelegateIdentityManager & identity_manager,
                                       p2p_interface & p2p)
    : _peer_manager(service, config.consensus_manager_config, *this)
    , _service(service)
    , _store(store)
    , _block_cache(block_cache)
    , _alarm(alarm)
    , _config(config)
    , _event_proposer(alarm, recall_handler)
    , _archiver(alarm, store, _event_proposer, recall_handler, block_cache)
    , _identity_manager(identity_manager)
    , _transition_state(EpochTransitionState::None)
    , _transition_delegate(EpochTransitionDelegate::None)
    , _transition_del_idx(NON_DELEGATE)
    , _p2p(p2p, block_cache)
{
    // TODO: remove static and dynamically modify _validate_sig_config based on tx acceptor addition / deletion during the software run
    // delegate mode, don't need to re-validate sig
    _validate_sig_config = _config.tx_acceptor_config.validate_sig && _config.tx_acceptor_config.tx_acceptors.empty();
}

void
ConsensusContainer::Start()
{
    // TODO: bootstrap first; all the operations below need to wait until bootstrapping is complete
    LOG_INFO(_log) << "ConsensusContainer::Start - Initializing ConsensusContainer.";
    // Initialize backup / fallback timers for each consensus type
    for (const ConsensusType & CT : CTs)
    {
        _timer_mutexes[CT];
        _timers.emplace(std::make_pair(CT, Timer(_service)));
        _timer_set[CT] = false;
        _timer_cancelled[CT] = false;
    }

    // Kick off epoch transition event scheduling
    LOG_INFO(_log) << "ConsensusContainer::Start - Starting epoch transition scheduling.";
    _event_proposer.Start([this](){
        this->EpochTransitionEventsStart();
    }, _store.is_first_epoch());

    // Kick off advertisement scheduling
    LOG_INFO(_log) << "ConsensusContainer::Start - Starting advertisement scheduling.";
    _identity_manager.CheckAdvertise(_cur_epoch_number, true);
}

void
ConsensusContainer::ActivateConsensus()
{
    std::lock_guard<std::mutex> lock(_mutex);

    /// 1. Determine role in current epoch
    uint8_t cur_delegate_idx;
    std::shared_ptr<ApprovedEB> approved_EB_cur;
    _identity_manager.IdentifyDelegates(QueriedEpochToNumber(QueriedEpoch::Current), cur_delegate_idx, approved_EB_cur);
    bool in_cur_epoch = cur_delegate_idx != NON_DELEGATE;

    /// 2. If Activated between ETES and ES, set transition delegate type
    std::shared_ptr<ApprovedEB> approved_EB_next;
    auto transitioning = TransitionEventsStarted(); // _event_proposer scheduling ensures transition state's correctness
    if (transitioning)
    {
        // Determine delegate role in the next epoch
        _identity_manager.IdentifyDelegates(QueriedEpochToNumber(QueriedEpoch::Next), _transition_del_idx, approved_EB_next);
        bool in_next_epoch = _transition_del_idx != NON_DELEGATE;

        // Set transition delegate type accordingly
        SetTransitionDelegate(in_cur_epoch, in_next_epoch);
    }

    /// 3. Build current EpochManager if the node is a current delegate; advertise endpoints.
    if (in_cur_epoch)
    {
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(cur_delegate_idx, *approved_EB_cur);

        _identity_manager.AdvertiseAndUpdateDB(_cur_epoch_number, cur_delegate_idx, approved_EB_cur);

        // current epoch manager's epoch connection can only be Current
        // (only EpochManager built by BuildUpcomingEpochManager can be in Transitioning,
        // and only a past epoch's EpochManager can be WaitingDisconnect)
        _binding_map[_cur_epoch_number] = CreateEpochManager(_cur_epoch_number, epoch_config,
                                                             EpochConnection::Current, approved_EB_cur);

        // Previous incoming address ads might have accumulated. We will have to establish connections here.
        EstablishConnections(_cur_epoch_number);
    }

    /// 4. If consensus is activated past ETES, set up next epoch's EpochManager if activated and in office next;
    /// also advertise endpoints
    if (_identity_manager.IsActiveInEpoch(QueriedEpoch::Next))  // caller locks _activation_mutex
    {
        if (transitioning)
        {
            // ETES didn't get to build upcoming EpochManager so we need to build here
            BuildUpcomingEpochManager(_transition_del_idx, approved_EB_next);  // handles checking _transition_delegate
        }
        else  // still have to perform one-time advertisement (this is separate from the scheduled ads)
        {
            _identity_manager.IdentifyDelegates(QueriedEpochToNumber(QueriedEpoch::Next), _transition_del_idx, approved_EB_next);
        }

        // Don't advertise for the next epoch if 1) not a delegate, or
        // 2) at stale epoch (missing epoch block for upcoming epoch)
        _identity_manager.AdvertiseAndUpdateDB(_cur_epoch_number + 1, _transition_del_idx, approved_EB_next);
    }

    std::string transition_summary;
    if (transitioning)
    {
        transition_summary = "; transitioning state: " + TransitionStateToName(_transition_state);
        transition_summary += "; transition delegate: " + TransitionDelegateToName(_transition_delegate);
        transition_summary += "; new epoch delegate index: " + std::to_string((int)_transition_del_idx);
    }
    else
    {
        _transition_del_idx = NON_DELEGATE;
    }

    LOG_INFO(_log) << "ConsensusContainer::ActivateConsensus - epoch transition enabled: "
                   << DelegateIdentityManager::IsEpochTransitionEnabled()
                   << "; current epoch number: " << _cur_epoch_number
                   << "; delegate is in current epoch: " << in_cur_epoch << ", index " << (int)cur_delegate_idx
                   << "; delegate is in next epoch: " << (_transition_del_idx != NON_DELEGATE) << transition_summary;

    /// 5. start archiver
    _archiver.Start(*this);
}

void
ConsensusContainer::DeactivateConsensus()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Stop archiver
    LOG_INFO(_log) << "ConsensusContainer::DeactivateConsensus - stopping archiver, current epoch number "
                   << _cur_epoch_number;
    _archiver.Stop();

    // clear any running EpochManager
    while (!_binding_map.empty())
    {
        auto entry = _binding_map.begin();
        assert(entry->first >= _cur_epoch_number -1 && entry->first <= _cur_epoch_number + 1);
        LOG_INFO(_log) << "ConsensusContainer::DeactivateConsensus - erasing EpochManager for epoch " << entry->first;
        _binding_map.erase(entry);
    }
    // Clear DelegateMap. TODO: this is just a crude temporary fix. DelegateMap may need its Reset method.
    DelegateMap::instance = nullptr;

    // Reset transition states
    _transition_delegate = EpochTransitionDelegate::None;
    _transition_del_idx = NON_DELEGATE;
}

bool
ConsensusContainer::TransitionEventsStarted()
{
    return _transition_state == EpochTransitionState::Connecting ||
           _transition_state == EpochTransitionState::EpochTransitionStart;
}

void
ConsensusContainer::UpcomingEpochSetUp()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // perform one-time advertisement (this is separate from the scheduled ads)
    std::shared_ptr<ApprovedEB> approved_EB_next;
    _identity_manager.IdentifyDelegates(QueriedEpochToNumber(QueriedEpoch::Next), _transition_del_idx, approved_EB_next);
    bool in_next_epoch = _transition_del_idx != NON_DELEGATE;

    // Don't advertise for the next epoch if 1) not a delegate, or
    // 2) at stale epoch (missing epoch block for upcoming epoch)
    _identity_manager.AdvertiseAndUpdateDB(_cur_epoch_number + 1, _transition_del_idx, approved_EB_next);

    // Set up (later than scheduled) upcoming epoch's consensus components
    if (TransitionEventsStarted())
    {
        // Determine transition delegate type
        uint8_t cur_delegate_idx;
        std::shared_ptr<ApprovedEB> approved_EB_cur;
        _identity_manager.IdentifyDelegates(QueriedEpochToNumber(QueriedEpoch::Current), cur_delegate_idx, approved_EB_cur);
        bool in_cur_epoch = cur_delegate_idx != NON_DELEGATE;

        SetTransitionDelegate(in_cur_epoch, in_next_epoch);

        // This method is only called when the node is Activated next; no need to check activation status again.
        BuildUpcomingEpochManager(_transition_del_idx, approved_EB_next);
    }
    else if (_transition_state == EpochTransitionState::None) // reset temporary value
    {
        _transition_del_idx = NON_DELEGATE;
    }

    LOG_INFO(_log) << "ConsensusContainer::UpcomingEpochSetUp - finished setting up for upcoming epoch "
                   << (_cur_epoch_number + 1);
}

std::shared_ptr<EpochManager>
ConsensusContainer::CreateEpochManager(
    uint epoch_number,
    const ConsensusManagerConfig &config,
    EpochConnection connection,
    const std::shared_ptr<ApprovedEB> & eb)
{
    auto res = std::make_shared<EpochManager>(_service, _store, _block_cache, _alarm, config,
                                              _archiver, _transition_state,
                                              _transition_delegate, connection, epoch_number, *this, *this, _p2p._p2p,
                                              config.delegate_id, _peer_manager, eb);
    res->Start();
    LOG_INFO(_log) << "ConsensusContainer::CreateEpochManager - created and started EpochManager"
                      " for epoch " << epoch_number
                   << "; current epoch number: " << _cur_epoch_number
                   << "; transition state: " << TransitionStateToName(_transition_state)
                   << "; transition delegate: " << TransitionDelegateToName(_transition_delegate)
                   << "; transition connection: " << TransitionConnectionToName(connection)
                   << "; delegate index: " << (int)(config.delegate_id);
    return res;
}

logos::process_return
ConsensusContainer::OnDelegateMessage(
    std::shared_ptr<DM> request,
    bool should_buffer)
{
    logos::process_return result;
    OptLock lock(_transition_state, _mutex);

    auto proposer_epoch_manager = GetProposerEpochManager();
    if (!proposer_epoch_manager)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnDelegateMessage transaction, the node is not a delegate; "
                          "activated now = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Current)
                       << "; activated next = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);
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
        proposer_epoch_manager->_request_manager->OnBenchmarkDelegateMessage(
            static_pointer_cast<DM>(request), result);
    }
    else
    {
        LOG_DEBUG(_log) << "ConsensusContainer::OnDelegateMessage: "
                        << "RequestType="
                        << GetRequestTypeField(request->type);
        proposer_epoch_manager->_request_manager->OnDelegateMessage(
            static_pointer_cast<DM>(request), result);
    }

    return result;
}

TxChannel::Responses
ConsensusContainer::OnSendRequest(vector<std::shared_ptr<DM>> &blocks)
{
    logos::process_return result;
    OptLock lock(_transition_state, _mutex);

    auto proposer_epoch_manager = GetProposerEpochManager();
    if (!proposer_epoch_manager)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnSendRequest transaction, the node is not a delegate.";
        return {{result.code,0}};
    }

    return proposer_epoch_manager->_request_manager->OnSendRequest(blocks);
}

void
ConsensusContainer::AttemptInitiateConsensus(ConsensusType CT)
{
    // Do nothing if we are retired
    OptLock lock(_transition_state, _mutex);

    bool archival = CT == ConsensusType::MicroBlock || CT == ConsensusType::Epoch;

    auto proposer_epoch_manager = GetProposerEpochManager(archival);
    if (!proposer_epoch_manager)
    {
        LOG_WARN(_log) << "ConsensusContainer::AttemptInitiateConsensus - the node is not currently a delegate "
                          "for consensus type " << ConsensusToName(CT)
                       << "; activated now = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Current)
                       << "; activated next = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);
        return;
    }

    switch (CT) {
        case ConsensusType::Request:
            proposer_epoch_manager->_request_manager->OnMessageQueued();
            break;
        case ConsensusType::MicroBlock:
        {
            proposer_epoch_manager->_micro_manager->OnMessageQueued();
            break;
        }
        case ConsensusType::Epoch:  // highly unlikely that epoch block doesn't complete consensus till next epoch start
            proposer_epoch_manager->_epoch_manager->OnMessageQueued();
            break;
        default:
            LOG_ERROR(_log) << "ConsensusContainer::AttemptInitiateConsensus - invalid consensus type";
    }
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

    auto proposer_epoch_manager = GetProposerEpochManager();
    if (!proposer_epoch_manager)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::BufferComplete transaction, the node is not a delegate; "
                          "activated now = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Current)
                       << "; activated next = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);
        return;
    }

    proposer_epoch_manager->_request_manager->BufferComplete(result);
}

logos::process_return
ConsensusContainer::OnDelegateMessage(
    std::shared_ptr<DelegateMessage<ConsensusType::MicroBlock>> message)
{
    OptLock lock(_transition_state, _mutex);
    logos::process_return result;
    using Request = DelegateMessage<ConsensusType::MicroBlock>;

    auto proposer_epoch_manager = GetProposerEpochManager();
    if (!proposer_epoch_manager)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnDelegateMessage microblock, the node is not a delegate; "
                          "activated now = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Current)
                       << "; activated next = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);
        return result;
    }

    message->delegates_epoch_number = proposer_epoch_manager->_epoch_number;
    proposer_epoch_manager->_micro_manager->OnDelegateMessage(
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

    auto proposer_epoch_manager = GetProposerEpochManager();
    if (!proposer_epoch_manager)
    {
        result.code = logos::process_result::not_delegate;
        LOG_WARN(_log) << "ConsensusContainer::OnDelegateMessage epoch, the node is not a delegate; "
                          "activated now = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Current)
                       << "; activated next = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);
        return result;
    }

    message->delegates_epoch_number = _cur_epoch_number;
    proposer_epoch_manager->_epoch_manager->OnDelegateMessage(
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

    if (_binding_map.find(epoch_number) == _binding_map.end())
    {
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder epoch manager is not available for "
                        << " delegate =" << (int)delegate_id
                        << " epoch =" << epoch_number;
        return false;
    }

    auto epoch = _binding_map[epoch_number];

    // After Epoch Start, a retiring EpochManager's connection state becomes WaitingDisconnect
    if (epoch->_connection_state == EpochConnection::WaitingDisconnect)
    {
        socket->close();
        LOG_WARN(_log) << "ConsensusContainer::PeerBinder: the node is not accepting connections.";
        return false;
    }

    LOG_INFO(_log) << "ConsensusContainer::PeerBinder, binding connection "
                    << epoch->GetConnectionName()
                    << " delegate " << epoch->GetDelegateName()
                    << " state " << epoch->GetStateName()
                    << ", delegate_id = " << unsigned(delegate_id)
                    << ", epoch_number = " << epoch_number;

    epoch->_netio_manager->OnConnectionAccepted(endpoint, socket, delegate_id);

    return true;
}

void
ConsensusContainer::EstablishConnections(uint32_t epoch_number)
{
    if (_binding_map.find(epoch_number) == _binding_map.end())
        return;

    auto epoch_manager = _binding_map[epoch_number];
    LOG_DEBUG(_log) << "ConsensusContainer::EstablishConnections - establishing connections for epoch " << epoch_number;

    for (uint8_t delegate_id = 0; delegate_id < NUM_DELEGATES; delegate_id++)
    {
        if (_identity_manager._address_ad.find({epoch_number, delegate_id}) != _identity_manager._address_ad.end())
        {
            auto ad = _identity_manager._address_ad[{epoch_number, delegate_id}];
            epoch_manager->_netio_manager->AddDelegate(delegate_id, ad.ip, ad.port);
        }
    }
}

uint8_t
ConsensusContainer::GetCurDelegateIdx()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_binding_map.find(GetCurEpochNumber()) != _binding_map.end())
    {
        return _binding_map[GetCurEpochNumber()]->GetDelegateId();
    }
    return NON_DELEGATE;
}

void
ConsensusContainer::LogEvent(const std::string & where, const uint32_t & new_epoch_num)
{
    LOG_INFO(_log) << "ConsensusContainer::" << where
                   << " - transition state: " << TransitionStateToName(_transition_state)
                   << "; transition delegate: " << TransitionDelegateToName(_transition_delegate)
                   << "; transition delegate index: " << (int)_transition_del_idx
                   << "; epoch " << (new_epoch_num - 1) << "==>" << new_epoch_num
                   << "; current epoch number: " << _cur_epoch_number
                   << "; activated now = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Current)
                   << "; activated next = " << _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);
}

void
ConsensusContainer::EpochTransitionEventsStart()
{
    LOG_DEBUG(_log) << "ConsensusContainer::" << __func__ << " - acquiring locks.";
    std::lock_guard<std::mutex> lock(_mutex);
    std::lock_guard<std::mutex> activation_lock(_identity_manager._activation_mutex);

    if (!DelegateIdentityManager::IsEpochTransitionEnabled())
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochTransitionEventsStart "
                           "epoch transition is not supported by this delegate";
        _binding_map[_cur_epoch_number + 1] = _binding_map[_cur_epoch_number];
        _binding_map.erase(_cur_epoch_number);
        _cur_epoch_number++;
        _identity_manager.ApplyActivationSchedule();
        return;
    }

    /// 1. Advance transition state
    _transition_state = EpochTransitionState::Connecting;

    if (_identity_manager.IsSleeved())
    {
        /// 2. If active in either the current or the next epoch, determine transition delegate type
        bool active_cur = _identity_manager.IsActiveInEpoch(QueriedEpoch::Current);
        bool active_next = _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);

        if (active_cur || active_next)
        {
            bool in_cur_epoch = false;
            if (GetProposerEpochManager())
            {
                // Delegate must be in current epoch if an EpochManager exists
                assert(active_cur);
                in_cur_epoch = true;
            }
            else
            {
                // Check if delegate is in current epoch but not activated
                uint8_t cur_delegate_idx;
                std::shared_ptr<ApprovedEB> approved_EB_cur;
                _identity_manager.IdentifyDelegates(QueriedEpochToNumber(QueriedEpoch::Current), cur_delegate_idx, approved_EB_cur);
                if (cur_delegate_idx != NON_DELEGATE)
                {
                    assert(!active_cur);
                    in_cur_epoch = true;
                }
            }

            std::shared_ptr<ApprovedEB> approved_EB_next;
            _identity_manager.IdentifyDelegates(QueriedEpochToNumber(QueriedEpoch::Next), _transition_del_idx, approved_EB_next);
            bool in_next_epoch = _transition_del_idx != NON_DELEGATE;

            // Set transition delegate type accordingly
            SetTransitionDelegate(in_cur_epoch, in_next_epoch);

            /// 3. Build and start epoch manager for next epoch, if activated
            if (active_next)
            {
                BuildUpcomingEpochManager(_transition_del_idx, approved_EB_next);
            }
        }
    }

    LogEvent(__func__, _cur_epoch_number + 1);

    /// 4. Schedule ETS
    ScheduleEpochTransitionStart();
}

void
ConsensusContainer::EpochTransitionStart()
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::lock_guard<std::mutex> activation_lock(_identity_manager._activation_mutex);

    if (_transition_state != EpochTransitionState::Connecting)
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochTransitionStart - Expecting state "
                       << TransitionStateToName(EpochTransitionState::Connecting)
                       << ". Current transition state is "
                       << TransitionStateToName(_transition_state)
                       << ". New epoch start may have been triggered by consensus peer messages.";
        return;
    }

    /// 1. Advance transition state
    _transition_state = EpochTransitionState::EpochTransitionStart;

    // Sanity check
    if ((_transition_delegate == EpochTransitionDelegate::New
        || _transition_delegate == EpochTransitionDelegate::Persistent)
        && _identity_manager.IsActiveInEpoch(QueriedEpoch::Next))
    {
        CheckEpochNull(_binding_map.find(_cur_epoch_number + 1) == _binding_map.end(), "EpochTransitionStart");
    }

    LogEvent(__func__, _cur_epoch_number + 1);

    /// 2. Schedule ES
    auto epoch_start = ArchivalTimer::GetNextEpochTime();
    auto lapse = epoch_start < EPOCH_TRANSITION_START ? epoch_start : EPOCH_TRANSITION_START;
    _alarm.add(lapse, std::bind(&ConsensusContainer::EpochStart, this));
}

void
ConsensusContainer::TransitionDelegate()
{
    if (_transition_delegate == EpochTransitionDelegate::Retiring || _transition_delegate == EpochTransitionDelegate::Persistent)
    {
        // returns _binding_map's _cur_epoch_number, which hasn't been incremented yet
        auto proposer_epoch_manager = GetProposerEpochManager();
        CheckEpochNull(!proposer_epoch_manager, "TransitionDelegate");

        if (_transition_delegate == EpochTransitionDelegate::Persistent)
        {
            bool active_next = _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);
            CheckEpochNull(active_next && !_binding_map[_cur_epoch_number + 1], "TransitionDelegate - Persistent");
        }

        proposer_epoch_manager->_connection_state = EpochConnection::WaitingDisconnect;
    }
}

void
ConsensusContainer::EpochStart()
{
    // TODO: need to support the scenario where a non-del node receives post-committed block with new epoch number
    std::lock_guard<std::mutex> lock(_mutex);
    std::lock_guard<std::mutex> activation_lock(_identity_manager._activation_mutex);

    // use _transition_state as gatekeeper
    if (_transition_state != EpochTransitionState::EpochTransitionStart)
    {
        LOG_WARN(_log) << "ConsensusContainer::EpochStart - Expecting state "
                       << TransitionStateToName(EpochTransitionState::EpochTransitionStart)
                       << ". Current transition state is "
                       << TransitionStateToName(_transition_state);

        if (_transition_state != EpochTransitionState::Connecting)
        {
            return;
        }

        // TODO: if state _is_ Connecting, then other delegate peers might be more than EPOCH_TRANSITION_START
        //  ahead of us and triggered this call through OnPostCommit or OnPrePrepareRejected,
        //  which would require a clock re-sync.
    }

    /// 1. Advance transition state
    _transition_state = EpochTransitionState::EpochStart;

    /// 2. Set the connection state of the current delegate (if any) to WaitingDisconnect
    bool active_cur = _identity_manager.IsActiveInEpoch(QueriedEpoch::Current);
    bool active_next = _identity_manager.IsActiveInEpoch(QueriedEpoch::Next);
    if (active_cur)
    {
        TransitionDelegate();
        /// 3. Stop archival if not activated next
        if (!active_next)
        {
            _archiver.Stop();
        }
    }
    else if (active_next)
    {
        _archiver.Start(*this);
    }

    /// 4. Increment epoch number counter
    // Note that epoch number must be incremented after TransitionDelegate() so as to not interfere with GetProposerEpochManager()
    _cur_epoch_number++;

    /// 5. Update activation settings
    // DelegateIdentityManager's Activation Schedule change is always coupled with epoch number increment
    _identity_manager.ApplyActivationSchedule();

    LogEvent(__func__, _cur_epoch_number);

    /// 6. Schedule ETE
    _alarm.add(EPOCH_TRANSITION_END, std::bind(&ConsensusContainer::EpochTransitionEnd, this));
}

void
ConsensusContainer::EpochTransitionEnd()
{
    std::lock_guard<std::mutex>   lock(_mutex);

    /// 1. Reset transition state
    _transition_state = EpochTransitionState::None;
    LogEvent(__func__, _cur_epoch_number);  // Note that logging takes place before delegate type / idx change

    /// 2. Clean up previous epoch's EpochManager, if any exists
    auto prev_epoch = _cur_epoch_number - 1;
    if (_binding_map.find(prev_epoch) != _binding_map.end())
    {
        assert(_transition_delegate != EpochTransitionDelegate::New);
        _binding_map.erase(prev_epoch);
    }

    /// 3. Change the current EpochManager's connection state
    // (EM only exists if delegate type is Persistent or New, and node is active next)
    if ((_transition_delegate == EpochTransitionDelegate::New ||
         _transition_delegate == EpochTransitionDelegate::Persistent) &&
        _identity_manager.IsActiveInEpoch(QueriedEpoch::Current))
    {
        auto proposer_epoch_manager = GetProposerEpochManager();
        CheckEpochNull(!proposer_epoch_manager, "EpochTransitionEnd");
        proposer_epoch_manager->_connection_state = EpochConnection::Current;
    }

    /// 4. Reset transition delegate type and index
    _transition_delegate = EpochTransitionDelegate::None;
    _transition_del_idx = NON_DELEGATE;
}

void
ConsensusContainer::ScheduleEpochTransitionStart()
{
    // TODO recall may have different timers
    Milliseconds lapse = EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START;
    Milliseconds epoch_start = ArchivalTimer::GetNextEpochTime();
    if (epoch_start > EPOCH_TRANSITION_START && epoch_start < EPOCH_DELEGATES_CONNECT)
    {
        lapse = epoch_start - EPOCH_TRANSITION_START;
    }
    else if (epoch_start < EPOCH_TRANSITION_START)
    {
        lapse = Milliseconds(0);
    }

    _alarm.add(lapse, std::bind(&ConsensusContainer::EpochTransitionStart, this));
}

void
ConsensusContainer::SetTransitionDelegate(bool in_cur_epoch, bool in_next_epoch)
{
    if (in_cur_epoch)
    {
        _transition_delegate = in_next_epoch ? EpochTransitionDelegate::Persistent : EpochTransitionDelegate::Retiring;
    }
    else if (in_next_epoch)
    {
        _transition_delegate = EpochTransitionDelegate::New;
    }
}

ConsensusManagerConfig
ConsensusContainer::BuildConsensusConfig(
    const uint8_t delegate_idx,
    const ApprovedEB & epoch)
{
   ConsensusManagerConfig config = _config.consensus_manager_config;

   config.delegate_id = delegate_idx;
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

void
ConsensusContainer::BuildUpcomingEpochManager(const uint8_t & delegate_idx, const std::shared_ptr<ApprovedEB> & approvedEb)
{
    if (_transition_delegate == EpochTransitionDelegate::New ||
        _transition_delegate == EpochTransitionDelegate::Persistent)
    {
        // New and Persistent delegates in the new delegate's set
        auto trans_epoch_num = _cur_epoch_number + 1;
        assert(_binding_map.find(trans_epoch_num) == _binding_map.end());
        ConsensusManagerConfig epoch_config = BuildConsensusConfig(delegate_idx, *approvedEb);
        _binding_map[trans_epoch_num] = CreateEpochManager(trans_epoch_num, epoch_config,
                                                           EpochConnection::Transitioning, approvedEb);
    }
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
                        << " - binding exists for current epoch: " << (_binding_map.find(GetCurEpochNumber()) != _binding_map.end())
                        << " for next epoch: " << (_binding_map.find(GetCurEpochNumber() + 1) != _binding_map.end())
                        << " _transition_state: " << TransitionStateToName(_transition_state)
                        << " _transition_delegate: " << TransitionDelegateToName(_transition_delegate);
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

std::shared_ptr<EpochManager>
ConsensusContainer::GetProposerEpochManager(bool archival)
{
    // During the period between EpochTransitionStart and EpochStart,
    // a `New` delegate's EpochManager can start processing before cur epoch number is incremented )
    bool transition_new_indicator = _transition_delegate == EpochTransitionDelegate::New;
    if (archival)
    {
        // Additionally, archival block proposed during epoch transition should be proposed
        // by the new delegate set even for `Persistent` delegates
        transition_new_indicator |= _transition_delegate == EpochTransitionDelegate::Persistent;
    }

    auto cur_binding_epoch_num = (_transition_state == EpochTransitionState::EpochTransitionStart && transition_new_indicator) ? GetCurEpochNumber() + 1 : GetCurEpochNumber();

    auto proposer = (_binding_map.find(cur_binding_epoch_num) != _binding_map.end())
                    ? _binding_map[cur_binding_epoch_num] : nullptr;
    LOG_DEBUG(_log) << "ConsensusContainer::GetProposerEpochManager "
                       "- transition state: " << TransitionStateToName(_transition_state)
                    << "; transition delegate: " << TransitionDelegateToName(_transition_delegate)
                    << "; current epoch number: " << _cur_epoch_number
                    << "; desired binding number: " << cur_binding_epoch_num
                    << "; proposer exists? " << (bool)proposer;
    return proposer;
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

        if (_binding_map.find(p2pconsensus_header.epoch_number) != _binding_map.end())
        {
            epoch = _binding_map[p2pconsensus_header.epoch_number];
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
    return (_binding_map.find(epoch_number) != _binding_map.end()) ? _binding_map[epoch_number] : nullptr;
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

    std::lock_guard<std::mutex> lock(_identity_manager._activation_mutex);
    auto epoch = GetEpochManager(prequel.epoch_number);

    LOG_DEBUG(_log) << "ConsensusContainer::OnAddressAd epoch " << prequel.epoch_number
                    << " delegate id " << (int)prequel.delegate_id
                    << " encr delegate id " << (int)prequel.encr_delegate_id
                    << " from epoch delegate id " << (epoch?(int)epoch->_delegate_id:255)
                    << " size " << size;

    std::string ip = "";
    uint16_t port = 0;

    if (_identity_manager.OnAddressAd(data, size, prequel, ip, port) &&
        epoch && epoch->GetDelegateId() == prequel.encr_delegate_id)
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
