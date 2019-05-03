#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>

#include <boost/log/sources/severity_feature.hpp>
#include <boost/log/core.hpp>

template<ConsensusType CT>
constexpr uint8_t ConsensusManager<CT>::BATCH_TIMEOUT_DELAY;

template<ConsensusType CT>
constexpr uint8_t ConsensusManager<CT>::DELIGATE_ID_MASK;

template<ConsensusType CT>
ConsensusManager<CT>::ConsensusManager(Service & service,
                                       Store & store,
                                       const Config & config,
                                       ConsensusScheduler & scheduler,
                                       MessageValidator & validator,
                                       p2p_interface & p2p,
                                       uint32_t epoch_number)
    : PrimaryDelegate(service, validator, epoch_number)
    , ConsensusP2pBridge(service, p2p, config.delegate_id)
    , _service(service)
    , _store(store)
    , _validator(validator)
    , _scheduler(scheduler)
    , _reservations(std::make_shared<ConsensusReservations>(store))
    , _persistence_manager(store, _reservations)
{
    _delegate_id = config.delegate_id;

    DelegateIdentityManager::GetCurrentEpoch(_store, _current_epoch);
    OnCurrentEpochSet();
}

template<ConsensusType CT>
void ConsensusManager<CT>::HandleRequest(std::shared_ptr<DelegateMessage> message,
                                         BlockHash & hash,
                                         logos::process_return & result)
{
    result.code = logos::process_result::progress;
    // SYL Integration fix: got rid of unnecessary lock here in favor of more granular locking

    LOG_INFO (_log) << "ConsensusManager<" << ConsensusToName(CT)
                    << ">::HandleRequest - hash: "
                    << hash.to_string();

    if(_state == ConsensusState::INITIALIZING)
    {
        result.code = logos::process_result::initializing;
        return;
    }

    if(IsPendingMessage(message))
    {
        result.code = logos::process_result::pending;
        LOG_INFO(_log) << "ConsensusManager<" << ConsensusToName(CT)
                       << ">::HandleRequest - pending message "
                       << hash.to_string();
        return;
    }

    if(!Validate(message, result))
    {
        LOG_INFO(_log) << "ConsensusManager<" << ConsensusToName(CT) << ">::HandleRequest - message validation failed."
                       << " Result code: "
                       << logos::ProcessResultToString(result.code)
                       << " hash: " << hash.to_string();
        return;
    }

    QueueMessage(message);
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnDelegateMessage(std::shared_ptr<DelegateMessage> block,
                                         logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto hash = block->Hash();

    HandleRequest(block, hash, result);

    if (result.code == logos::process_result::progress)
    {
        OnMessageQueued();
    }
}

template<>
std::vector<std::pair<logos::process_result, BlockHash>>
ConsensusManager<ConsensusType::Request>::OnSendRequest(
    std::vector<std::shared_ptr<DelegateMessage>>& blocks)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    Responses response;
    logos::process_return result;
    bool res = false;

    for (const auto & block : blocks)
    {
         auto hash = block->Hash();
         HandleRequest(block, hash, result);
         if (result.code == logos::process_result::progress)
         {
             res = true;
         }
         else
         {
             hash = 0;
         }
         response.push_back({result.code, hash});
    }

    if (res)
    {
        OnMessageQueued();
    }

    return response;
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnMessageQueued()
{
    if (_ongoing) return;  // primary will call this method again once consensus is completed
    if(!PrePrepareQueueEmpty())
    {
        if (_ongoing) // just in case two calls to method pass both checks above
        {
            LOG_WARN(_log) << "ConsensusManager<" << ConsensusToName(CT) << ">::OnMessageQueued - "
                           << "unlikely scenario where two calls pass both checks.";
            return;
        }
        // InitiateConsensus should only be called
        // when no consensus session is currently going on
        _ongoing = true;
        _scheduler.CancelTimer(CT);
        InitiateConsensus();
    }
    else
    {
        // Get most imminent timeout, if any, and schedule timer
        auto imminent_timeout = GetHandler().GetImminentTimeout();
        if (imminent_timeout == Min_DT) return;
        LOG_DEBUG(_log) << "ConsensusManager<" << ConsensusToName(CT) << ">::OnMessageQueued - "
                        << "imminent timeout is " << imminent_timeout << ", scheduling timer";
        _scheduler.ScheduleTimer(CT, imminent_timeout);
    }
}

template<ConsensusType CT>
void ConsensusManager<CT>::Send(const void * data, size_t size)
{
    std::lock_guard<std::mutex> lock(_connection_mutex);

    for(auto conn : _connections)
    {
        conn->Send(data, size);
    }
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnConsensusReached()
{
    if (!AlreadyPostCommitted())  // Always execute below for Request
    {
        auto & pre_prepare (PrePrepareGetCurr());
        ApprovedBlock block(pre_prepare, _post_prepare_sig, _post_commit_sig);

        ApplyUpdates(block, _delegate_id);

        BlocksCallback::Callback<CT>(block);

        // Helpful for benchmarking
        {
            static uint64_t messages_stored = 0;
            messages_stored += GetStoredCount();

            LOG_DEBUG(_log) << "ConsensusManager<"
                            << ConsensusToName(CT)
                            << "> - Stored "
                            << messages_stored
                            << " blocks.";
        }

        std::vector<uint8_t> buf;
        block.Serialize(buf, true, true);
        this->Broadcast(buf.data(), buf.size(), block.type);
    }
    BeginNextRound();
}

template<ConsensusType CT>
void ConsensusManager<CT>::BeginNextRound()
{
    SetPreviousPrePrepareHash(_pre_prepare_hash);
    PrePreparePopFront();

    // SYL Integration: finally set ongoing consensus indicator to false to allow next round of consensus to being
    _ongoing = false;

    // Don't need to lock _state_mutex here because there should only be
    // one call to OnConsensusReached per consensus round
    OnMessageQueued();
}

template<ConsensusType CT>
void ConsensusManager<CT>::InitiateConsensus(bool reproposing)
{
    LOG_INFO(_log) << "Initiating "
                   << ConsensusToName(CT)
                   << " consensus, reproposing " << reproposing;

    // Build next PrePrepare message (timestamp is also added in PrePrepareGetNext)
    auto & pre_prepare = PrePrepareGetNext(reproposing);
    if (CT == ConsensusType::Request)
    {
        pre_prepare.delegates_epoch_number = _epoch_number;
    }

    // SYL Integration: if we don't want to lock _state_mutex here it is important to
    // call OnConsensusInitiated before AdvanceState (otherwise PrimaryDelegate might
    // mistakenly process previous consensus messages from backups in this new round,
    // since ProceedWithMessage checks _state first then _cur_hash).
    OnConsensusInitiated(pre_prepare);
    AdvanceState(ConsensusState::PRE_PREPARE);

    pre_prepare.preprepare_sig = _pre_prepare_sig;
    LOG_DEBUG(_log) << "JSON representation: " << pre_prepare.ToJson();
    PrimaryDelegate::Send<PrePrepare>(pre_prepare);
}

template<ConsensusType CT>
void
ConsensusManager<CT>::QueueMessage(
        std::shared_ptr<DelegateMessage> message)
{
    uint8_t designated_delegate_id = DesignatedDelegate(message);

    if(designated_delegate_id == _delegate_id)
    {
        LOG_DEBUG(_log) << "ConsensusManager<" << ConsensusToName(CT) << ">::QueueMessage primary";
        QueueMessagePrimary(message);
    }
    else
    {
        LOG_DEBUG(_log) << "ConsensusManager<" << ConsensusToName(CT) << ">::QueueMessage secondary";
        QueueMessageSecondary(message);
    }
}

template<ConsensusType CT>
void
ConsensusManager<CT>::QueueMessagePrimary(
    std::shared_ptr<DelegateMessage> message)
{
    GetHandler().OnMessage(message);
}

template<ConsensusType CT>
void
ConsensusManager<CT>::QueueMessageSecondary(
    std::shared_ptr<DelegateMessage> message)
{
    GetHandler().OnMessage(message, GetSecondaryTimeout());
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::PrePrepareQueueEmpty()
{
    return InternalQueueEmpty() && GetHandler().PrimaryEmpty();
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::Contains(
    const BlockHash &hash)
{
    return InternalContains(hash) || GetHandler().Contains(hash);
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::IsPendingMessage(
    std::shared_ptr<DelegateMessage> message)
{
    return Contains(message->Hash());
}

template<ConsensusType CT>
std::shared_ptr<MessageParser>
ConsensusManager<CT>::BindIOChannel(std::shared_ptr<IOChannel> iochannel,
                                    const DelegateIdentities & ids)
{
    std::lock_guard<std::mutex> lock(_connection_mutex);

    auto connection = MakeBackupDelegate(iochannel, ids);
    _connections.push_back(connection);

    return connection;
}

template<ConsensusType CT>
void
ConsensusManager<CT>::OnNetIOError(uint8_t delegate_id)
{
    std::lock_guard<std::mutex> lock(_connection_mutex);

    for (auto it = _connections.begin(); it != _connections.end(); ++it)
    {
        if ((*it)->IsRemoteDelegate(delegate_id))
        {
            (*it)->CleanUp();
            _connections.erase(it);
            break;
        }
    }
}

template<ConsensusType CT>
void
ConsensusManager<CT>::OnP2pTimeout(const ErrorCode &ec) {

    if (ec && ec == boost::asio::error::operation_aborted)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(_connection_mutex);

    uint64_t quorum = 0;
    logos::uint128_t vote = 0;
    logos::uint128_t stake = 0;
    for (auto it = _connections.begin(); it != _connections.end(); ++it)
    {
        auto direct = (*it)->PrimaryDirectlyConnected()?1:0;
        (*it)->ResetConnectCount();
        vote += direct * _weights[(*it)->RemoteDelegateId()].vote_weight;
        stake += direct * _weights[(*it)->RemoteDelegateId()].stake_weight;
    }

    if (!(vote >= _vote_quorum && stake >= _stake_quorum))
    {
        LOG_DEBUG(_log) << "ConsensusManager<" << ConsensusToName(CT)
                        << ">::OnP2pTimeout, scheduling p2p timer "
                        << " vote " << vote << "/" << _vote_quorum
                        << " stake " << stake << "/" << _stake_quorum;
        std::weak_ptr<ConsensusManager<CT>> this_w = std::dynamic_pointer_cast<ConsensusManager<CT>>(shared_from_this());
        ConsensusP2pBridge::ScheduleP2pTimer([this_w](const ErrorCode &ec) {
            auto this_s = GetSharedPtr(this_w, "ConsensusManager<", ConsensusToName(CT),
                    ">::OnP2pTimeout, object destroyed");
            if (!this_s)
            {
                return;
            }
            this_s->OnP2pTimeout(ec);
        });
    }
    else
    {
        LOG_DEBUG(_log) << "ConsensusManager<" << ConsensusToName(CT)
                        << ">::OnP2pTimeout, DELEGATE " << (int)_delegate_id << " DISABLING P2P ";
        ConsensusP2pBridge::EnableP2p(false);
    }
}

template<ConsensusType CT>
void
ConsensusManager<CT>::EnableP2p(bool enable)
{
    ConsensusP2pBridge::EnableP2p(enable);

    if (enable)
    {
        std::weak_ptr<ConsensusManager<CT>> this_w =
                std::dynamic_pointer_cast<ConsensusManager<CT>>(shared_from_this());
        ConsensusP2pBridge::ScheduleP2pTimer([this_w](const ErrorCode &ec) {
            auto this_s = GetSharedPtr(this_w, "ConsensusManager<", ConsensusToName(CT),
                                       ">::EnableP2p, object destroyed");
            if (!this_s)
            {
                return;
            }
            this_s->OnP2pTimeout(ec);
        });
    }
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::ProceedWithRePropose()
{
    // ignore if the old delegate's set, the new delegate's set will pick it up
    auto notifier = GetSharedPtr(_events_notifier, "ConsensusManager<", ConsensusToName(CT),
                                ">::ProceedWithRePropose, object destroyed");
    if (!notifier)
    {
        return false;
    }

    return  (notifier->GetState() == EpochTransitionState::None &&
             notifier->GetDelegate() == EpochTransitionDelegate::None) ||
            notifier->GetConnection() == EpochConnection::Transitioning;
}

template<ConsensusType CT>
void
ConsensusManager<CT>::OnQuorumFailed()
{
    if (!ProceedWithRePropose()) return;

    if (AlreadyPostCommitted())
    {
        BeginNextRound();
    }
    else
    {
        LOG_ERROR(_log) << "ConsensusManager::OnQuorumFailed<" << ConsensusToName(CT)
                        << "> - PRIMARY DELEGATE IS ENABLING P2P!!!";
        {
            std::lock_guard<std::mutex> lock(_connection_mutex);

            for (auto it = _connections.begin(); it != _connections.end(); ++it)
            {
                (*it)->ResetConnectCount();
            }
        }
        EnableP2p(true);

        AdvanceState(ConsensusState::VOID);

        InitiateConsensus(true);
    }
}

template class ConsensusManager<ConsensusType::Request>;
template class ConsensusManager<ConsensusType::MicroBlock>;
template class ConsensusManager<ConsensusType::Epoch>;
