#include <rai/consensus/consensus_manager.hpp>

#include <rai/node/node.hpp>

constexpr uint8_t ConsensusManager::BATCH_TIMEOUT_DELAY;

ConsensusManager::ConsensusManager(Service & service,
                                   Store & store,
                                   rai::alarm & alarm,
                                   Log & log,
                                   const Config & config)
    : PrimaryDelegate(_validator)
    , _delegates(config.stream_peers)
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

    EstablishDelegateIds(config.local_address);

    for(uint8_t i = 0; i < _delegates.size(); ++i)
    {
        auto & peer = _delegates[i];
        auto endpoint = Endpoint(boost::asio::ip::make_address_v4(peer),
                                 local_endpoint.port());

        if(i == _delegate_id)
        {
            continue;
        }

        if(_delegate_id < i)
        {
            ConsensusConnection::DelegateIdentities ids{_delegate_id, i};

            _connections.push_back(
                    std::make_shared<ConsensusConnection>(service, alarm, endpoint,
                                                          this, _persistence_manager,
                                                          _validator, ids));
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

    BOOST_LOG (_log) << "ConsensusManager::OnSendRequest() - hash: " << block->hash().to_string();

    if(!Validate(block, result))
    {
        BOOST_LOG (_log) << "ConsensusManager - block validation for send request failed. Result code: "
                         << rai::ProcessResultToString(result.code)
                         << " hash " << block->hash().to_string();
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

    BOOST_LOG (_log) << "ConsensusManager::OnBenchmarkSendRequest() - hash: " << block->hash().to_string();

    _using_buffered_blocks = true;
    _buffer.push_back(block);
}

void ConsensusManager::OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket> socket)
{
    ConsensusConnection::DelegateIdentities ids{_delegate_id,
                                                GetDelegateId(endpoint.address().to_string())};

    _connections.push_back(std::make_shared<ConsensusConnection>(socket, _alarm, endpoint,
                                                                 this, _persistence_manager,
                                                                 _validator, ids));
}

void ConsensusManager::Send(const void * data, size_t size)
{
    for(auto conn : _connections)
    {
        conn->Send(data, size);
    }
}

bool ConsensusManager::Validate(std::shared_ptr<rai::state_block> block, rai::process_return & result)
{
    return _persistence_manager.Validate(*block, result, _delegate_id);
}

void ConsensusManager::OnConsensusReached()
{
    _persistence_manager.ApplyUpdates(_handler.GetNextBatch(), _delegate_id);

    // Helpful for benchmarking
    //
    {
        static uint64_t messages_stored = 0;
        messages_stored += _handler.GetNextBatch().block_count;
        BOOST_LOG(_log) << "ConsensusManager - Stored " << messages_stored << " blocks.";
    }

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

    OnConsensusInitiated(batch);

    _validator.Sign(batch);
    Send(&batch, sizeof(BatchStateBlock));

    _state = ConsensusState::PRE_PREPARE;
}

bool ConsensusManager::ReadyForConsensus()
{
    if(_using_buffered_blocks)
    {

        return StateReadyForConsensus() && _handler.BatchFull();
    }

    return StateReadyForConsensus() && !_handler.Empty();
}

bool ConsensusManager::StateReadyForConsensus()
{
    return _state == ConsensusState::VOID || _state == ConsensusState::POST_COMMIT;
}

void ConsensusManager::SendBufferedBlocks()
{
    rai::process_return unused;

    for(uint64_t i = 0; _buffer.size() && i < CONSENSUS_BATCH_SIZE; ++i)
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
    BOOST_LOG(_log) << "Buffered " << _buffer.size() << " blocks.";

    result.code = rai::process_result::buffering_done;
    SendBufferedBlocks();
}

void ConsensusManager::EstablishDelegateIds(const std::string & local_address)
{
    std::sort(_delegates.begin(), _delegates.end());

    _delegate_id = GetDelegateId(local_address);

    if(_delegate_id == _delegates.size())
    {
        throw std::runtime_error("ConsensusManager - Failed to find local address in delegate list.");
    }
}

uint8_t ConsensusManager::GetDelegateId(const std::string & address)
{
    return std::distance(_delegates.begin(),
                         std::find(_delegates.begin(), _delegates.end(),
                                   address));
}
