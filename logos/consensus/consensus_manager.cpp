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
                                       EpochEventsNotifier & events_notifier)
    : PrimaryDelegate(service, validator)
    , _store(store)
    , _validator(validator)
    , _secondary_handler(SecondaryRequestHandlerInstance(service, this))
    , _events_notifier(events_notifier)
    , _reservations(std::make_shared<Reservations>(store))
    , _persistence_manager(store, _reservations)
{
    _delegate_id = config.delegate_id;

    DelegateIdentityManager::GetCurrentEpoch(_store, _current_epoch);
    OnCurrentEpochSet();
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnSendRequest(std::shared_ptr<Request> block,
                                         logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto hash = block->Hash();

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
    OnRequestQueued();
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnRequestQueued()
{
    if(ReadyForConsensus())
    {
        InitiateConsensus();
    }
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnRequestReady(
    std::shared_ptr<Request> block)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

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

    _prev_pre_prepare_hash = _pre_prepare_hash;

    PrePreparePopFront();

    if(!PrePrepareQueueEmpty())
    {
        InitiateConsensus();
    }
}

template<ConsensusType CT>
void ConsensusManager<CT>::InitiateConsensus()
{
    LOG_INFO(_log) << "Initiating "
                   << ConsensusToName(CT)
                   << " consensus.";

    auto & pre_prepare = PrePrepareGetNext();
    pre_prepare.previous = _prev_pre_prepare_hash;

    OnConsensusInitiated(pre_prepare);

    _state = ConsensusState::PRE_PREPARE;

    pre_prepare.preprepare_sig = _pre_prepare_sig;
    PrimaryDelegate::Send<PrePrepare>(pre_prepare);
}

template<ConsensusType CT>
bool ConsensusManager<CT>::ReadyForConsensus()
{
    return StateReadyForConsensus() && !PrePrepareQueueEmpty();
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

template class ConsensusManager<ConsensusType::BatchStateBlock>;
template class ConsensusManager<ConsensusType::MicroBlock>;
template class ConsensusManager<ConsensusType::Epoch>;
