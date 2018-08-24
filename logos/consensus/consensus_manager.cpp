#include <logos/consensus/consensus_manager.hpp>

#include <logos/node/node.hpp>

template<ConsensusType consensus_type>
constexpr uint8_t ConsensusManager<consensus_type>::BATCH_TIMEOUT_DELAY;

template<ConsensusType consensus_type>
ConsensusManager<consensus_type>::ConsensusManager(Service & service,
                                   Store & store,
                                   logos::alarm & alarm,
                                   Log & log,
                                   const Config & config,
                                   DelegateKeyStore & key_store,
                                   MessageValidator & validator)
    : PrimaryDelegate(validator)
    , _persistence_manager(store, log)
    , _key_store(key_store)
	, _validator(validator)
    , _alarm(alarm)
    , _delegate_id(config.delegate_id)
{
}

template<ConsensusType consensus_type>
void ConsensusManager<consensus_type>::OnSendRequest(std::shared_ptr<RequestMessage<consensus_type>> block, logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    BOOST_LOG (_log) << "ConsensusManager<consensus_type>::OnSendRequest() - hash: " << block->hash().to_string();

    if(!Validate(block, result))
    {
        BOOST_LOG(_log) << "ConsensusManager - block validation for send request failed. Result code: "
                        << logos::ProcessResultToString(result.code)
                        << " hash " << block->hash().to_string();
        return;
    }

    QueueRequest(block);

    if(ReadyForConsensusExt())
    {
        InitiateConsensus();
    }
}

template<ConsensusType consensus_type>
void ConsensusManager<consensus_type>::Send(const void * data, size_t size)
{
    std::lock_guard<std::mutex> lock(_connection_mutex);

    for(auto conn : _connections)
    {
        conn->Send(data, size);
    }
}

template<ConsensusType consensus_type>
void ConsensusManager<consensus_type>::OnConsensusReached()
{
    ApplyUpdates(PrePrepareGetNext(), _delegate_id);

    // Helpful for benchmarking
    //
    {
        static uint64_t messages_stored = 0;
        messages_stored += OnConsensusReachedStoredCount();
        BOOST_LOG(_log) << "ConsensusManager - Stored " << messages_stored << " blocks.";
    }

    PrePreparePopFront();

    if(OnConsensusReachedExt())
    {
        return;
    }

    if(!PrePrepareQueueEmpty())
    {
        InitiateConsensus();
    }
}

template<ConsensusType consensus_type>
void ConsensusManager<consensus_type>::InitiateConsensus()
{
    auto & pre_prepare = PrePrepareGetNext();

    OnConsensusInitiated(pre_prepare);

    _validator.Sign(pre_prepare);
    Send(&pre_prepare, sizeof(PrePrepareMessage<consensus_type>));

    _state = ConsensusState::PRE_PREPARE;
}

template<ConsensusType consensus_type>
bool ConsensusManager<consensus_type>::ReadyForConsensus()
{
    return StateReadyForConsensus() && !PrePrepareQueueEmpty();
}

template<ConsensusType consensus_type>
bool ConsensusManager<consensus_type>::StateReadyForConsensus()
{
    return _state == ConsensusState::VOID || _state == ConsensusState::POST_COMMIT;
}

template<ConsensusType consensus_type>
std::shared_ptr<IConsensusConnection> ConsensusManager<consensus_type>::BindIOChannel(std::shared_ptr<IIOChannel> iochannel, const DelegateIdentities & ids)
{
    auto consensus_connection = std::make_shared<ConsensusConnection<consensus_type>>(iochannel, _alarm,
                                                   this, _persistence_manager,
                                                   _key_store, _validator, ids);
    _connections.push_back(consensus_connection);
    return consensus_connection;
}

template class ConsensusManager<ConsensusType::BatchStateBlock>;
template class ConsensusManager<ConsensusType::MicroBlock>;
template class ConsensusManager<ConsensusType::Epoch>;
