#include <logos/consensus/consensus_manager.hpp>

template<ConsensusType CT>
constexpr uint8_t ConsensusManager<CT>::BATCH_TIMEOUT_DELAY;

template<ConsensusType CT>
constexpr uint8_t ConsensusManager<CT>::DELIGATE_ID_MASK;

template<ConsensusType CT>
ConsensusManager<CT>::ConsensusManager(Service & service,
                                       Store & store,
                                       Log & log,
                                       const Config & config,
                                       DelegateKeyStore & key_store,
                                       MessageValidator & validator,
                                       const std::string & callback_address,
                                       const uint16_t & callback_port,
                                       const std::string & callback_target)
    : PrimaryDelegate(validator)
    , _service(service)
    , _log(log)
    , _key_store(key_store)
    , _validator(validator)
    , _delegate_id(config.delegate_id)
    , _secondary_handler(service, *this)
    , _callback_address (callback_address)
    , _callback_port (callback_port)
    , _callback_target (callback_target)
{
    _block_callback.add([this](PrePrepare preprepare){
        _service.post  ([this, preprepare]() {
            if (!_callback_address.empty ())
            {
                auto body (std::make_shared<std::string> (preprepare.SerializeJson ()));
                auto address (_callback_address);
                auto port (_callback_port);
                auto target (std::make_shared<std::string> (_callback_target));
                auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (_service));
                resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [this, address, port, target, body, resolver](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
                    if (!ec)
                    {
                        for (auto i (i_a), n (boost::asio::ip::tcp::resolver::iterator{}); i != n; ++i)
                        {
                            auto sock (std::make_shared<boost::asio::ip::tcp::socket> (_service));
                            sock->async_connect (i->endpoint (), [this, target, body, sock, address, port](boost::system::error_code const & ec) {
                                if (!ec)
                                {
                                    auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
                                    req->method (boost::beast::http::verb::post);
                                    req->target (*target);
                                    req->version (11);
                                    req->insert (boost::beast::http::field::host, address);
                                    req->insert (boost::beast::http::field::content_type, "application/json");
                                    req->body () = *body;
                                    req->prepare_payload ();
                                    boost::beast::http::async_write (*sock, *req, [this, sock, address, port, req](boost::system::error_code const & ec, size_t bytes_transferred) {
                                        if (!ec)
                                        {
                                            auto sb (std::make_shared<boost::beast::flat_buffer> ());
                                            auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
                                            boost::beast::http::async_read (*sock, *sb, *resp, [this, sb, resp, sock, address, port](boost::system::error_code const & ec, size_t bytes_transferred) {
                                                if (!ec)
                                                {
                                                    if (resp->result () == boost::beast::http::status::ok)
                                                    {
                                                    }
                                                    else
                                                    {
                                                        if (true)  // TODO: add back callback_logging config, same as all below
                                                        {
                                                            BOOST_LOG (_log) << boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ());
                                                        }
                                                    }
                                                }
                                                else
                                                {
                                                    if (true)
                                                    {
                                                        BOOST_LOG (_log) << boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ());
                                                    }
                                                };
                                            });
                                        }
                                        else
                                        {
                                            if (true)
                                            {
                                                BOOST_LOG (_log) << boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ());
                                            }
                                        }
                                    });
                                }
                                else
                                {
                                    if (true)
                                    {
                                        BOOST_LOG (_log) << boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ());
                                    }
                                }
                            });
                        }
                    }
                    else
                    {
                        if (true)
                        {
                            BOOST_LOG (_log) << boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ());
                        }
                    }
                });
            }
        });
    });
}

template<ConsensusType CT>
void ConsensusManager<CT>::OnSendRequest(std::shared_ptr<Request> block,
                                         logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto hash = block->hash();

    BOOST_LOG (_log) << "ConsensusManager<" << ConsensusToName(CT) <<
        ">::OnSendRequest() - hash: " << hash.to_string();

    if (IsPendingRequest(block))
    {
        result.code = logos::process_result::pending;
        BOOST_LOG(_log) << "ConsensusManager<" << ConsensusToName(CT) <<
            "> - pending request " << hash.to_string();
        return;
    }

    if(!Validate(block, result))
    {
        BOOST_LOG(_log) << "ConsensusManager - block validation for send request failed. Result code: "
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
void
ConsensusManager<CT>::OnRequestReady(
    std::shared_ptr<Request> block)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    QueueRequestPrimary(block);
    OnRequestQueued();
}

template<ConsensusType CT>
void
ConsensusManager<CT>::OnPrePrepare(
    const PrePrepare & block)
{
    _secondary_handler.OnPrePrepare(block);
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
    auto pre_prepare (PrePrepareGetNext());
    ApplyUpdates(pre_prepare, _delegate_id);
    _block_callback(pre_prepare);  // TODO: would rather use a shared pointer to avoid copying the whole BlockList

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
