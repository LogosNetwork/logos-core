#include <logos/consensus/consensus_manager.hpp>
#include <logos/node/delegate_identity_manager.hpp>
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
                                       MessageValidator & validator,
                                       p2p_interface & p2p,
                                       uint32_t epoch_number)
    : PrimaryDelegate(service, validator, epoch_number)
    , ConsensusP2pBridge<CT>(service, p2p, config.delegate_id)
    , _service(service)
    , _store(store)
    , _validator(validator)
    , _waiting_list(GetWaitingList(service))
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
                    << ">::OnDelegateMessage() - hash: "
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
                       << "> - pending message "
                       << hash.to_string();
        return;
    }

    if(!Validate(message, result))
    {
        LOG_INFO(_log) << "ConsensusManager - message validation failed."
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
    bool res = true;

    for (auto block : blocks)
    {
         auto hash = block->Hash();
         HandleRequest(block, hash, result);
         if (result.code != logos::process_result::progress)
         {
             hash = 0;
             res = false;
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
    if(ReadyForConsensus())
    {
        // SYL integration fix: InitiateConsensus should only be called
        // when no consensus session is currently going on
        _ongoing = true;
        InitiateConsensus();
    }
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnMessageReady(
    std::shared_ptr<DelegateMessage> block)
{
    QueueMessagePrimary(block);
    OnMessageQueued();
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnPostCommit(
    const PrePrepare & block)
{
    _waiting_list.OnPostCommit(block);
}

template<ConsensusType CT>
logos::block_store &
ConsensusManager<CT>::GetStore()
{
    return _store;
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
    auto & pre_prepare (PrePrepareGetCurr());
    ApprovedBlock block(pre_prepare, _post_prepare_sig, _post_commit_sig);

    ApplyUpdates(block, _delegate_id);

    BlocksCallback::Callback<CT>(block);

    // Helpful for benchmarking
    //
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

    auto & pre_prepare = PrePrepareGetNext();

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
bool ConsensusManager<CT>::ReadyForConsensus()
{
    if(_ongoing)
    {
        return false;
    }
    return !PrePrepareQueueEmpty();
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::IsPrePrepared(const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_connection_mutex);

    for(auto conn : _connections)
    {
        if(conn->IsPrePrepared(hash))
        {
            return true;
        }
    }

    return false;
}

template<ConsensusType CT>
void
ConsensusManager<CT>::QueueMessage(
        std::shared_ptr<DelegateMessage> message)
{
    uint8_t designated_delegate_id = DesignatedDelegate(message);

    if(designated_delegate_id == _delegate_id)
    {
        LOG_DEBUG(_log) << "ConsensusManager<CT>::QueueMessage primary";
        QueueMessagePrimary(message);
    }
    else
    {
        LOG_DEBUG(_log) << "ConsensusManager<CT>::QueueMessage secondary";
        QueueMessageSecondary(message);
    }
}

template<ConsensusType CT>
void
ConsensusManager<CT>::QueueMessageSecondary(
    std::shared_ptr<DelegateMessage> message)
{
    _waiting_list.OnMessage(message);
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::SecondaryContains(
    const BlockHash &hash)
{
    return _waiting_list.Contains(hash);
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::IsPendingMessage(
    std::shared_ptr<DelegateMessage> message)
{
    auto hash = message->Hash();

    return (PrimaryContains(hash) ||
            SecondaryContains(hash) ||
            IsPrePrepared(hash));
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
ConsensusManager<CT>::UpdateMessagePromoter()
{
    auto promoter = std::dynamic_pointer_cast<MessagePromoter<CT>>(shared_from_this());
    _waiting_list.UpdateMessagePromoter(promoter);
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
        ConsensusP2pBridge<CT>::ScheduleP2pTimer([this_w](const ErrorCode &ec) {
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
        ConsensusP2pBridge<CT>::EnableP2p(false);
    }
}

template<ConsensusType CT>
void
ConsensusManager<CT>::EnableP2p(bool enable)
{
    ConsensusP2pBridge<CT>::EnableP2p(enable);

    if (enable)
    {
        std::weak_ptr<ConsensusManager<CT>> this_w =
                std::dynamic_pointer_cast<ConsensusManager<CT>>(shared_from_this());
        ConsensusP2pBridge<CT>::ScheduleP2pTimer([this_w](const ErrorCode &ec) {
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
    if (ProceedWithRePropose())
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
