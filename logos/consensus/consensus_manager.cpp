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
                                       EpochEventsNotifier & events_notifier,
                                       p2p_interface & p2p)
    : PrimaryDelegate(service, validator)
    , _store(store)
    , _validator(validator)
    , _secondary_handler(SecondaryRequestHandlerInstance(service, this))
    , _events_notifier(events_notifier)
    , _reservations(std::make_shared<Reservations>(store))
    , _persistence_manager(store, _reservations)
    , _consensus_p2p(p2p, config.delegate_id)
{
    _delegate_id = config.delegate_id;

    DelegateIdentityManager::GetCurrentEpoch(_store, _current_epoch);
    OnCurrentEpochSet();
}

template<ConsensusType CT>
void ConsensusManager<CT>::HandleRequest(std::shared_ptr<Request> block,
                                         BlockHash &hash,
                                         logos::process_return & result)
{
    // SYL Integration fix: got rid of unnecessary lock here in favor of more granular locking

    LOG_INFO (_log) << "ConsensusManager<" << ConsensusToName(CT)
                    << ">::OnSendRequest() - hash: "
                    << hash.to_string();

    if(_state == ConsensusState::INITIALIZING)
    {
        result.code = logos::process_result::initializing;
        return;
    }

    if(IsPendingRequest(block))
    {
        result.code = logos::process_result::pending;
        LOG_INFO(_log) << "ConsensusManager<" << ConsensusToName(CT)
                       << "> - pending request "
                       << hash.to_string();
        return;
    }

    if(!Validate(block, result))
    {
        LOG_INFO(_log) << "ConsensusManager - block validation for send request failed."
                       << " Result code: "
                       << logos::ProcessResultToString(result.code)
                       << " hash: " << hash.to_string();
        return;
    }

    QueueRequest(block);
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnSendRequest(std::shared_ptr<Request> block,
                                         logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto hash = block->Hash();

    HandleRequest(block, hash, result);

    OnRequestQueued();

}

template<>
std::vector<std::pair<logos::process_result, BlockHash>>
ConsensusManager<ConsensusType::BatchStateBlock>::OnSendRequest(
    std::vector<std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>>>& blocks)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    Responses response;
    logos::process_return result;

    for (auto block : blocks)
    {
         auto hash = block->Hash();
         HandleRequest(block, hash, result);
         if (result.code != logos::process_result::progress)
         {
             hash = 0;
         }
         response.push_back({result.code, hash});
    }

    OnRequestQueued();

    return response;
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnRequestQueued()
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
void ConsensusManager<CT>::OnRequestReady(
    std::shared_ptr<Request> block)
{
    QueueRequestPrimary(block);
    OnRequestQueued();
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnPostCommit(
    const PrePrepare & block)
{
    _secondary_handler.OnPostCommit(block);
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

    // TODO: would rather use a shared pointer
    //       to avoid copying the whole RequestList
    //       for BSB.
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
    _consensus_p2p.ProcessOutputMessage(buf.data(), buf.size());

    SetPreviousPrePrepareHash(_pre_prepare_hash);

    PrePreparePopFront();

    // SYL Integration: finally set ongoing consensus indicator to false to allow next round of consensus to being
    _ongoing = false;

    // Don't need to lock _state_mutex here because there should only be
    // one call to OnConsensusReached per consensus round
    OnRequestQueued();
}

template<ConsensusType CT>
void ConsensusManager<CT>::InitiateConsensus()
{
    LOG_INFO(_log) << "Initiating "
                   << ConsensusToName(CT)
                   << " consensus.";

    auto & pre_prepare = PrePrepareGetNext();

    // SYL Integration: if we don't want to lock _state_mutex here it is important to
    // call OnConsensusInitiated before AdvanceState (otherwise PrimaryDelegate might
    // mistakenly process previous consensus messages from backups in this new round,
    // since ProceedWithMessage checks _state first then _cur_hash).
    OnConsensusInitiated(pre_prepare);
    AdvanceState(ConsensusState::PRE_PREPARE);

    pre_prepare.preprepare_sig = _pre_prepare_sig;
    LOG_DEBUG(_log) << "JSON representation: " << pre_prepare.SerializeJson();
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
ConsensusManager<CT>::QueueRequest(
        std::shared_ptr<Request> request)
{
    uint8_t designated_delegate_id = DesignatedDelegate(request);

    if(designated_delegate_id == _delegate_id)
    {
        LOG_DEBUG(_log) << "ConsensusManager<CT>::QueueRequest primary";
        QueueRequestPrimary(request);
    }
    else
    {
        LOG_DEBUG(_log) << "ConsensusManager<CT>::QueueRequest secondary";
        QueueRequestSecondary(request);
    }
}

template<ConsensusType CT>
void
ConsensusManager<CT>::QueueRequestSecondary(
    std::shared_ptr<Request> request)
{
    _secondary_handler.OnRequest(request);
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::SecondaryContains(
    const BlockHash &hash)
{
    return _secondary_handler.Contains(hash);
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::IsPendingRequest(
    std::shared_ptr<Request> block)
{
    auto hash = block->Hash();

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
ConsensusManager<CT>::UpdateRequestPromoter()
{
    _secondary_handler.UpdateRequestPromoter(this);
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

template class ConsensusManager<ConsensusType::Request>;
template class ConsensusManager<ConsensusType::MicroBlock>;
template class ConsensusManager<ConsensusType::Epoch>;
