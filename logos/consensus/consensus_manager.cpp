#include <logos/consensus/consensus_manager.hpp>

#include <logos/node/node.hpp>

constexpr uint8_t ConsensusManager::BATCH_TIMEOUT_DELAY;

ConsensusManager::ConsensusManager(Service & service,
                                   Store & store,
                                   logos::alarm & alarm,
                                   Log & log,
                                   const Config & config)
    : PrimaryDelegate(_validator)
    , _client(Endpoint(boost::asio::ip::make_address_v4(config.callback_address),
                       config.callback_port),
              service)
    , _delegates(config.delegates)
    , _persistence_manager(store, log)
    , _validator(_key_store)
    , _alarm(alarm)
    , _peer_acceptor(service, log,
                     Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                              config.peer_port),
                     this)
    , _delegate_id(config.delegate_id)
{
    std::set<Address> server_endpoints;

    auto local_endpoint(Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                        config.peer_port));

    _key_store.OnPublicKey(_delegate_id, _validator.GetPublicKey());

    for(auto & delegate : _delegates)
    {
        auto endpoint = Endpoint(boost::asio::ip::make_address_v4(delegate.ip),
                                 local_endpoint.port());

        if(delegate.id == _delegate_id)
        {
            continue;
        }

        if(_delegate_id < delegate.id)
        {
            ConsensusConnection::DelegateIdentities ids{_delegate_id, delegate.id};

            std::lock_guard<std::mutex> lock(_connection_mutex);
            _connections.push_back(
                    std::make_shared<ConsensusConnection>(service, alarm, endpoint,
                                                          this, _persistence_manager,
                                                          _key_store,_validator, ids));
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

void ConsensusManager::OnSendRequest(std::shared_ptr<logos::state_block> block, logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    BOOST_LOG(_log) << "ConsensusManager::OnSendRequest() - hash: " << block->hash().to_string();

    if(!Validate(block, result))
    {
        BOOST_LOG(_log) << "ConsensusManager - block validation for send request failed. Result code: "
                        << logos::ProcessResultToString(result.code)
                        << " hash " << block->hash().to_string();
        return;
    }

    _handler.OnRequest(block);

    if(ReadyForConsensus())
    {
        InitiateConsensus();
    }
}

void ConsensusManager::OnBenchmarkSendRequest(std::shared_ptr<logos::state_block> block, logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    BOOST_LOG(_log) << "ConsensusManager::OnBenchmarkSendRequest() - hash: " << block->hash().to_string();

    _using_buffered_blocks = true;
    _buffer.push_back(block);
}

void ConsensusManager::OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket> socket)
{
    auto entry = std::find_if(_delegates.begin(), _delegates.end(),
                              [&](const Config::Delegate & delegate){
                                  return delegate.ip == endpoint.address().to_string();
                              });

    assert(entry != _delegates.end());

    ConsensusConnection::DelegateIdentities ids{_delegate_id, entry->id};

    std::lock_guard<std::mutex> lock(_connection_mutex);
    _connections.push_back(std::make_shared<ConsensusConnection>(socket, _alarm, endpoint,
                                                                 this, _persistence_manager,
                                                                 _key_store,_validator, ids));
}

void ConsensusManager::Send(const void * data, size_t size)
{
    std::lock_guard<std::mutex> lock(_connection_mutex);

    for(auto conn : _connections)
    {
        conn->Send(data, size);
    }
}

bool ConsensusManager::Validate(std::shared_ptr<logos::state_block> block, logos::process_return & result)
{
	if(logos::validate_message(block->hashables.account, block->hash(), block->signature))
	{
        BOOST_LOG(_log) << "ConsensusManager - Validate, bad signature: " << block->signature.to_string()
		                << " account: " << block->hashables.account.to_string();

        result.code = logos::process_result::bad_signature;
        return false;
	}

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

    _client.OnBatchBlock(_handler.GetNextBatch());

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
        return StateReadyForConsensus() && (_handler.BatchFull() ||
                                           (_buffer.empty() && !_handler.Empty()));
    }

    return StateReadyForConsensus() && !_handler.Empty();
}

bool ConsensusManager::StateReadyForConsensus()
{
    return _state == ConsensusState::VOID || _state == ConsensusState::POST_COMMIT;
}

void ConsensusManager::SendBufferedBlocks()
{
    logos::process_return unused;

    for(uint64_t i = 0; _buffer.size() && i < CONSENSUS_BATCH_SIZE; ++i)
    {
        OnSendRequest(_buffer.front(), unused);
        _buffer.pop_front();
    }

    if(!_buffer.size())
    {
        BOOST_LOG(_log) << "ConsensusManager - No more buffered blocks for consensus" << std::endl;
    }
}

void ConsensusManager::BufferComplete(logos::process_return & result)
{
    BOOST_LOG(_log) << "Buffered " << _buffer.size() << " blocks.";

    result.code = logos::process_result::buffering_done;
    SendBufferedBlocks();
}
