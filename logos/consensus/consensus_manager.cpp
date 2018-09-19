#include <logos/consensus/consensus_manager.hpp>
//#include <logos/consensus/epoch/epoch_consensus_connection.hpp>
//#include <logos/consensus/microblock/microblock_consensus_connection.hpp>
//#include <logos/consensus/batchstateblock/batchblock_consensus_connection.hpp>

#include <logos/node/node.hpp>

template<ConsensusType CT>
constexpr uint8_t ConsensusManager<CT>::BATCH_TIMEOUT_DELAY;

template<ConsensusType CT>
ConsensusManager<CT>::ConsensusManager(Service & service,
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
{}

template<ConsensusType CT>
void ConsensusManager<CT>::OnSendRequest(std::shared_ptr<Request> block,
                                         logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    BOOST_LOG (_log) << "ConsensusManager<" << ConsensusToName(CT) <<
        ">::OnSendRequest() - hash: " << block->hash().to_string();

    if(!Validate(block, result))
    {
        BOOST_LOG(_log) << "ConsensusManager - block validation for send request failed. Result code: "
                        << logos::ProcessResultToString(result.code)
                        << " hash: " << block->hash().to_string();
        return;
    }

    QueueRequest(block);

    if(ReadyForConsensus())
    {
        InitiateConsensus();
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
    ApplyUpdates(PrePrepareGetNext(), _delegate_id);

    // Helpful for benchmarking
    //
    {
        static uint64_t messages_stored = 0;
        messages_stored += GetStoredCount();
        BOOST_LOG(_log) << "ConsensusManager<" << ConsensusToName(CT) <<
            "> - Stored " << messages_stored << " blocks.";
    }

    PrePreparePopFront();

    if(!PrePrepareQueueEmpty())
    {
        InitiateConsensus();
    }
}

template<ConsensusType CT>
void ConsensusManager<CT>::InitiateConsensus()
{
    auto & pre_prepare = PrePrepareGetNext();

    OnConsensusInitiated(pre_prepare);

    _validator.Sign(pre_prepare);
    Send(&pre_prepare, sizeof(PrePrepare));

    _state = ConsensusState::PRE_PREPARE;
}

template<ConsensusType CT>
bool ConsensusManager<CT>::ReadyForConsensus()
{
    return StateReadyForConsensus() && !PrePrepareQueueEmpty();
}

template<ConsensusType CT>
bool ConsensusManager<CT>::StateReadyForConsensus()
{
    return _state == ConsensusState::VOID || _state == ConsensusState::POST_COMMIT;
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
