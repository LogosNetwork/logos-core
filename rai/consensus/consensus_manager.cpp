#include <rai/consensus/consensus_manager.hpp>

#include <rai/node/node.hpp>

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

constexpr uint8_t ConsensusManager::BATCH_TIMEOUT_DELAY;

ConsensusManager::ConsensusManager(Service & service,
                                   Store & store,
                                   rai::alarm & alarm,
                                   Log & log,
                                   const Config & config)
    : PrimaryDelegate(_validator)
    , _delegates(config.delegates)
    , _persistence_manager(store, log)
	, _validator(_key_store)
    , _alarm(alarm)
    , _service(service)
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
                                                          _key_store, _validator, ids));
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

    BOOST_LOG(_log) << "ConsensusManager::OnSendRequest() - hash: " << block->hash().to_string();

    if(!Validate(block, result))
    {
        BOOST_LOG(_log) << "ConsensusManager - block validation for send request failed. Result code: "
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
                                                                 _key_store, _validator, ids));
}

void ConsensusManager::Send(const void * data, size_t size)
{
    std::lock_guard<std::mutex> lock(_connection_mutex);

    for(auto conn : _connections)
    {
        conn->Send(data, size);
    }
}

bool ConsensusManager::Validate(std::shared_ptr<rai::state_block> block, rai::process_return & result)
{
	if(rai::validate_message(block->hashables.account, block->hash(), block->signature))
	{
        BOOST_LOG(_log) << "ConsensusManager - Validate, bad signature: " << block->signature.to_string()
		                << " account: " << block->hashables.account.to_string();

        result.code = rai::process_result::bad_signature;
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

    // BatchBlock Callback
    BatchBlockCallback(_handler.GetNextBatch());

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
    rai::process_return unused;

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

void ConsensusManager::BufferComplete(rai::process_return & result)
{
    BOOST_LOG(_log) << "Buffered " << _buffer.size() << " blocks.";

    result.code = rai::process_result::buffering_done;
    SendBufferedBlocks();
}

void ConsensusManager::BatchBlockCallback(const BatchStateBlock & block)
{
    auto sock(std::make_shared<Socket>(_service));

    auto address(_callback_endpoint.address().to_string());
    auto port(_callback_endpoint.port());

    BOOST_LOG(_log) << "ConsensusManager::BatchBlockCallback()";

    sock->async_connect(_callback_endpoint, [this, sock, address, port, block](boost::system::error_code const & ec) {

        BOOST_LOG(_log) << "ConsensusManager::BatchBlockCallback - on connect";

        if(ec)
        {
            BOOST_LOG(_log) << boost::str(boost::format("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message());
            return;
        }

        for(uint64_t i = 0; i < block.block_count; ++i)
        {
            boost::property_tree::ptree event;
            event.add("hash", block.blocks[i].hash().to_string());
            event.add("account", block.blocks[i].hashables.account.to_string());
            event.add("amount", block.blocks[i].hashables.amount.to_string_dec());

//            event.add("account", account_a.to_account());
//            event.add("hash", block_a->hash().to_string());
//            std::string block_text;
//            block_a->serialize_json(block_text);
//            event.add("block", block_text);
//            event.add("amount", amount_a.to_string_dec());

            std::stringstream ostream;
            boost::property_tree::write_json(ostream, event);
            ostream.flush();
            auto body(std::make_shared<std::string>(ostream.str()));

            auto req(std::make_shared<boost::beast::http::request<boost::beast::http::string_body>>());
            req->method(boost::beast::http::verb::post);
            req->target("/");
            req->version(11);
            req->insert(boost::beast::http::field::host, address);
            req->insert(boost::beast::http::field::content_type, "application/json");
            req->body() = *body;
            //req->prepare(*req);
            //boost::beast::http::prepare(req);
            req->prepare_payload();

            boost::beast::http::async_write(*sock, *req, [this, sock, address, port, req](boost::system::error_code const & ec, size_t bytes_transferred) {


                BOOST_LOG(_log) << "ConsensusManager::BatchBlockCallback - on write";

                if(ec)
                {
                    BOOST_LOG(_log) << boost::str(boost::format("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message());
                    return;
                }

                auto sb(std::make_shared<boost::beast::flat_buffer>());
                auto resp(std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>());

                boost::beast::http::async_read(*sock, *sb, *resp, [this, sb, resp, sock, address, port](boost::system::error_code const & ec, size_t bytes_transferred) {
                    if(ec)
                    {
                        BOOST_LOG(_log) << boost::str(boost::format("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message());
                        return;
                    }

                    if(resp->result() != boost::beast::http::status::ok)
                    {
                        BOOST_LOG(_log) << boost::str(boost::format("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result());
                    }
                });

            });
        }
    });
}
