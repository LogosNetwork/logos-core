#include <rai/consensus/consensus_manager.hpp>

#include <rai/node/node.hpp>

constexpr uint8_t ConsensusManager::BATCH_TIMEOUT_DELAY;

ConsensusManager::ConsensusManager(Service & service,
                                   Store & store,
                                   rai::alarm & alarm,
                                   Log & log,
                                   const Config & config)
    : PrimaryDelegate(log,
                      _validator)
    , _handler(alarm)
    , _persistence_manager(store, log)
    , _alarm(alarm)
    , _peer_acceptor(service, log,
                     Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                              config.peer_port),
                     this)
{
    std::set<Address> server_endpoints;

    auto local_endpoint(Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                        config.peer_port));

    for(const auto & peer : config.stream_peers)
    {
        auto endpoint = Endpoint(boost::asio::ip::make_address_v4(peer),
                                 local_endpoint.port());

        if(endpoint == local_endpoint)
        {
            continue;
        }

        if(ConnectionPolicy()(local_endpoint, endpoint))
        {
            _connections.push_back(
                    std::make_shared<ConsensusConnection>(service, alarm, _log,
                                                          endpoint, this, _persistence_manager,
                                                          _validator));
        }
        else
        {
            server_endpoints.insert(endpoint.address());
        }
    }

    if(server_endpoints.size())
    {
        _peer_acceptor.Start(server_endpoints);
    }
}

void ConsensusManager::OnSendRequest(std::shared_ptr<rai::state_block> block, rai::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    BOOST_LOG (_log) << "ConsensusManager::OnSendRequest()";

    if(!Validate(block, result))
    {
        BOOST_LOG (_log) << "ConsensusManager - block validation for send request failed.";
        return;
    }

    _handler.OnRequest(block);

    if(ReadyForConsensus())
    {
        InitiateConsensus();
    }
}

void ConsensusManager::OnBenchmarkSendRequest(std::shared_ptr<rai::state_block> block, rai::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    BOOST_LOG (_log) << "ConsensusManager::OnBenchmarkSendRequest()";

    _using_buffered_blocks = true;
    _buffer.push_back(block);
}

void ConsensusManager::OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket> socket)
{
    _connections.push_back(std::make_shared<ConsensusConnection>(socket, _alarm, _log,
                                                                 endpoint, this, _persistence_manager,
                                                                 _validator));
}

void ConsensusManager::Send(const void * data, size_t size)
{
    for(auto conn : _connections)
    {
        conn->Send(data, size);
    }
}

// TODO: Compare new send message against others
//       sent in this batch.
//
bool ConsensusManager::Validate(std::shared_ptr<rai::state_block> block, rai::process_return & result)
{
    return _persistence_manager.Validate(*block, result);
}

void ConsensusManager::OnConsensusReached()
{
    _persistence_manager.StoreBatchMessage(_handler.GetNextBatch());
    _persistence_manager.ApplyBatchMessage(_handler.GetNextBatch());

    _handler.PopFront();

    if(_using_buffered_blocks)
    {
        SendBufferedBlocks();
        return;
    }

    if(!_handler.Empty())
    {
        InitiateConsensus();
    }
}

void ConsensusManager::InitiateConsensus()
{
    auto & batch = _handler.GetNextBatch();

    OnConsensusInitiated(batch.Hash());
    Send(&batch, sizeof(BatchStateBlock));

    _state = ConsensusState::PRE_PREPARE;
}

bool ConsensusManager::ReadyForConsensus()
{
    return StateReadyForConsensus() && !_handler.Empty();
}

bool ConsensusManager::StateReadyForConsensus()
{
    return _state == ConsensusState::VOID || _state == ConsensusState::POST_COMMIT;
}

void ConsensusManager::SendBufferedBlocks()
{
    rai::process_return unused;

    for(uint8_t i = 0; i < _buffer.size() && i < CONSENSUS_BATCH_SIZE; ++i)
    {
        OnSendRequest(_buffer.front(), unused);
        _buffer.pop_front();
    }

    if(!_buffer.size())
    {
        BOOST_LOG (_log) << "ConsensusManager - No more buffered blocks for consensus" << std::endl;
    }
}

void ConsensusManager::BufferComplete(rai::process_return & result)
{
    result.code = rai::process_result::buffering_done;
    SendBufferedBlocks();
}
