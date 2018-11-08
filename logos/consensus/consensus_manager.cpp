#include <logos/consensus/consensus_manager.hpp>

#include <boost/log/core.hpp>
#include <boost/log/sources/severity_feature.hpp>

template<ConsensusType CT>
constexpr uint8_t ConsensusManager<CT>::BATCH_TIMEOUT_DELAY;

template<ConsensusType CT>
constexpr uint8_t ConsensusManager<CT>::DELIGATE_ID_MASK;

template<ConsensusType CT>
ConsensusManager<CT>::ConsensusManager(Service & service,
                                       Store & store,
                                       const Config & config,
                                       DelegateKeyStore & key_store,
				       MessageValidator & validator,
				       p2p_interface & p2p)
    : PrimaryDelegate(service, validator)
    , _secondary_handler(service, *this)
    , _store(store)
    , _key_store(key_store)
    , _validator(validator)
    , _delegate_id(config.delegate_id)
    , _consensus_p2p(log, p2p, _delegate_id,
	[this](const Prequel & message, MessageType mtype, uint8_t delegate_id) {
		return mtype == MessageType::Pre_Prepare  ? this->Validate((PrePrepare  &)message, delegate_id)
		     : mtype == MessageType::Post_Prepare ? this->_validator.Validate((PostPrepareMessage<CT> &)message, delegate_id)
		     : mtype == MessageType::Post_Commit  ? this->_validator.Validate((PostCommitMessage<CT>  &)message, delegate_id)
		     : false;
	},
	boost::bind(&ConsensusManager<CT>::ApplyUpdates, this, _1, _2))
{
    store.batch_tip_get(_delegate_id, _prev_batch_hash);
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnSendRequest(std::shared_ptr<Request> block,
                                         logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto hash = block->hash();

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
void ConsensusManager<CT>::Send(const void * data, size_t size, bool propagate)
{
    std::lock_guard<std::mutex> lock(_connection_mutex);

    for(auto conn : _connections)
    {
        conn->Send(data, size);
    }

    _consensus_p2p.ProcessOutputMessage((const uint8_t *)data, size, propagate);
}

template<ConsensusType CT>
bool ConsensusManager<CT>::OnP2pReceive(const void * data, size_t size) {
    return _consensus_p2p.ProcessInputMessage((const uint8_t *)data, size);
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnConsensusReached()
{
    ApplyUpdates(PrePrepareGetNext(), _delegate_id);

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

    _prev_batch_hash = _cur_batch_hash;

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
    pre_prepare.previous = _prev_batch_hash;

    OnConsensusInitiated(pre_prepare);

    _state = ConsensusState::PRE_PREPARE;

    _validator.Sign(pre_prepare);
    Send(&pre_prepare, sizeof(PrePrepare));
}

template<ConsensusType CT>
bool ConsensusManager<CT>::ReadyForConsensus()
{
    return StateReadyForConsensus() && !PrePrepareQueueEmpty();
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::IsPrePrepared(const logos::block_hash & hash)
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
        QueueRequestPrimary(request);
    }
    else
    {
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
    const logos::block_hash &hash)
{
    return _secondary_handler.Contains(hash);
}

template<ConsensusType CT>
bool
ConsensusManager<CT>::IsPendingRequest(
    std::shared_ptr<Request> block)
{
    auto hash = block->hash();

    return (PrimaryContains(hash) ||
            SecondaryContains(hash) ||
            IsPrePrepared(hash));
}

template<ConsensusType CT>
std::shared_ptr<PrequelParser>
ConsensusManager<CT>::BindIOChannel(std::shared_ptr<IOChannel> iochannel,
                                    const DelegateIdentities & ids)
{
    auto connection = MakeConsensusConnection(iochannel, ids);
    _connections.push_back(connection);

    return connection;
}

template class ConsensusManager<ConsensusType::BatchStateBlock>;
template class ConsensusManager<ConsensusType::MicroBlock>;
template class ConsensusManager<ConsensusType::Epoch>;
