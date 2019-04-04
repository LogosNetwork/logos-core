#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/pull_connection.hpp>
#include <logos/bootstrap/tip_connection.hpp>
#include <logos/bootstrap/attempt.hpp>

#include <boost/asio/buffer.hpp>

namespace Bootstrap
{
    SocketTimeout::SocketTimeout (Socket & socket)
    : ticket (0)
    , socket (socket)
    {}

    void SocketTimeout::start (std::chrono::steady_clock::time_point timeout_a)
    {
        auto ticket_l (++ticket);
        std::weak_ptr<Socket> socket_w (socket.shared ());
        socket.alarm.add (timeout_a, [socket_w, ticket_l]()
        {
            // NOTE: If we timeout, we disconnect.
            if (auto socket_l = socket_w.lock ())
            {
                if (socket_l->timeout.ticket == ticket_l)
                {
                    socket_l->OnNetworkError();
                    LOG_DEBUG(socket_l->log) << "timeout: "
                                    <<"remote_endpoint " << socket_l->peer;
                }
            }
        });
    }

    void SocketTimeout::stop ()
    {
        LOG_DEBUG(socket.log) << "socket_timeout::stop:";
        ++ticket;
    }

    /////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////

    using BoostError = boost::system::error_code;

    Socket::Socket(logos::tcp_endpoint & endpoint, logos::alarm & alarm)
    : peer(endpoint)
    , alarm(alarm)
    , socket(alarm.service)
    , timeout (*this)
    , disconnected(true)
    {
        LOG_TRACE(log) << "bootstrap_socket::"<<__func__ << " client side";
    }

    Socket::Socket(boost::asio::ip::tcp::socket &socket_a, logos::alarm & alarm)
    : peer(socket_a.remote_endpoint())
    , alarm(alarm)
    , socket(std::move(socket_a))
    , timeout (*this)
    , disconnected(true)
    {
        LOG_TRACE(log) << "bootstrap_socket::"<<__func__ << " server side";
    }

    Socket::~Socket()
    {
        LOG_TRACE(log) << "bootstrap_socket::"<<__func__;
        Disconnect();
    }

    void Socket::Connect (std::function<void(bool)> ConnectComplete)
    {
        LOG_TRACE(log) << "bootstrap_socket::"<<__func__
                << " this: " << this
                << " timeout_ms: " << CONNECT_TIMEOUT_MS;
        timeout.start (std::chrono::steady_clock::now () +
                        std::chrono::milliseconds(CONNECT_TIMEOUT_MS));
        auto this_l = shared();
        socket.async_connect (peer, [this, this_l, ConnectComplete](BoostError const & ec)
        {
            timeout.stop();
            if (!ec)
            {
                LOG_TRACE(log) << "Socket::connect: connected";
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    disconnected = false;
                }
                ConnectComplete(true);
            }
            else
            {
                LOG_ERROR(log) << "Socket::connect: network error: ec.message: " << ec.message();
                ConnectComplete(false);
            }
        });
    }

    void Socket::Disconnect()
    {
        /*
         * From boost doc:
         * Note that, even if the function indicates an error, the underlying descriptor is closed.
         * To graceful closure of a connected socket, call shutdown() before closing the socket.
         */
        std::lock_guard<std::mutex> lock(mtx);
        if(!disconnected)
        {
            LOG_TRACE(log) << "Socket::Disconnect " << this;
            disconnected = true;
            BoostError ec;
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket.close (ec);
        }

    }

    void Socket::AsyncSend(std::shared_ptr<std::vector<uint8_t>> buf, SendComplete cb, uint32_t timeout_ms)
    {
        LOG_TRACE(log) << "Socket::"<<__func__ << " this: " << this << " timeout_ms: " << timeout_ms;
        if(timeout_ms > timeout_disabled)
        {
            timeout.start (std::chrono::steady_clock::now () + std::chrono::milliseconds(timeout_ms));
        }
        boost::asio::async_write (socket, boost::asio::buffer(buf->data(), buf->size()),
                [this, cb, timeout_ms](BoostError const & ec, size_t size_a)
                {
                    if(timeout_ms > timeout_disabled)
                    {
                        timeout.stop();
                    }
                    if (!ec)
                    {
                        LOG_TRACE(log) << "Socket::AsyncSend: sent data, size=" << size_a;
                        cb(true);
                    }else{
                        LOG_ERROR(log) << "Socket::AsyncSend: Network error " << ec.message ();
                        cb(false);
                    }
                });
    }

    void Socket::AsyncReceive(ReceiveComplete cb, uint32_t timeout_ms)
    {
        LOG_TRACE(log) << "Socket::"<<__func__ << " this: " << this << " timeout_ms: " << timeout_ms;
        if(timeout_ms > timeout_disabled)
        {
            timeout.start (std::chrono::steady_clock::now () + std::chrono::milliseconds(timeout_ms));
        }
        boost::asio::async_read (socket, boost::asio::buffer(header_buf.data (), MessageHeader::WireSize),
            [this, cb, timeout_ms](BoostError const & ec, size_t size_a)
            {
                if (!ec)
                {
                    LOG_TRACE(log) << "Socket::AsyncReceive received header data";
                    assert (size_a == MessageHeader::WireSize);
                    logos::bufferstream stream (header_buf.data (), size_a);
                    bool error = false;
                    MessageHeader header(error, stream);
                    if(error || ! header.Validate())
                    {
                        LOG_ERROR(log) << "Socket::AsyncReceive Header error";
                        cb(false, header, nullptr);
                        return;
                    }

                    boost::asio::async_read (socket,
                            boost::asio::buffer(receive_buf.data(), header.payload_size),
                        [this, cb, timeout_ms, header](BoostError const & ec, size_t size_a)
                        {
                            if(timeout_ms > timeout_disabled)
                            {
                                timeout.stop();
                            }
                            if (!ec) {
                                LOG_TRACE(log) << "Socket::AsyncReceive received data";
                                assert (size_a == header.payload_size);
                                cb(true, header, receive_buf.data());
                            }else{
                                LOG_ERROR(log) << "Socket::AsyncReceive Network error " << ec.message ();
                                cb(false, header, nullptr);
                                return;
                            }
                        });

                }else{
                    LOG_ERROR(log) << "Socket::AsyncReceive Network error " << ec.message ();
                    MessageHeader header;
                    cb(false, header, nullptr);
                    return;
                }
            });
    }

    std::shared_ptr<Socket> Socket::shared ()
    {
        auto x = shared_from_this();
        return x;
    }
    boost::asio::ip::address Socket::PeerAddress() const
    {
        return peer.address();
    }
    ///////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////

    BootstrapClient::BootstrapClient (BootstrapAttempt & attempt_a,
            logos::tcp_endpoint & endpoint_a)
    : Socket (endpoint_a, attempt_a.alarm)
    , attempt (attempt_a)
    {
        LOG_TRACE(log) << "bootstrap_client::"<<__func__;
    }

    BootstrapClient::~BootstrapClient ()
    {
        LOG_TRACE(log) << "bootstrap_client::"<<__func__;
    }

    void BootstrapClient::OnNetworkError(bool black_list)
    {
        LOG_TRACE(log) << "bootstrap_client::"<<__func__<< " this=" <<this;
        Socket::Disconnect();
        attempt.remove_connection(shared(), black_list);
    }
    void BootstrapClient::Release()
    {
        LOG_TRACE(log) << "bootstrap_client::"<<__func__<< " this=" <<this;
        attempt.pool_connection(shared());
    }

    std::shared_ptr<BootstrapClient> BootstrapClient::shared ()
    {
        //LOG_TRACE(log) << "bootstrap_client::"<<__func__;
        return shared_from_base<BootstrapClient>();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////

    BootstrapServer::BootstrapServer (BootstrapListener & listener,
            BoostSocket & socket_a,
            Store & store)
    : Socket (socket_a, listener.alarm)
    , listener(listener)
    , store (store)
    {
        LOG_TRACE(log) << "bootstrap_server::"<<__func__;
    }

    BootstrapServer::~BootstrapServer ()
    {
        LOG_TRACE(log) << "bootstrap_server::"<<__func__;
    }

    void BootstrapServer::receive_request ()
    {
        LOG_TRACE(log) << "bootstrap_server::"<<__func__;
        auto this_l = shared();
        AsyncReceive([this, this_l](bool good, MessageHeader header, uint8_t * buf)
            {
                dispatch(good, header, buf);
            });
    }
    void BootstrapServer::OnNetworkError(bool black_list)
    {
        LOG_TRACE(log) << "bootstrap_server::"<<__func__<< " this=" <<this;
        Socket::Disconnect();
        listener.remove_connection(shared());//server does not black list
    }
    void BootstrapServer::Release()
    {
        LOG_TRACE(log) << "bootstrap_server::"<<__func__<< " this=" <<this;
        receive_request ();
    }

    void BootstrapServer::dispatch (bool good, MessageHeader header, uint8_t * buf)
    {
        LOG_TRACE(log) << "bootstrap_server::"<<__func__ <<" good=" << good;
        bool error = false;
        if (good)
        {

#ifdef DUMP_BLOCK_DATA    //TODO delete after integration tests
            {
                std::stringstream stream;
                for(size_t i = 0; i < header.payload_size; ++i)
                {
                    stream << std::hex << std::noshowbase << std::setw (2) << std::setfill ('0') << (unsigned int)(buf[i]);
                }
                LOG_TRACE(log) << "bootstrap_server::"<<__func__ <<" data:" << stream.str ();
            }
#endif
            logos::bufferstream stream (buf, header.payload_size);

            switch (header.type)
            {
                case MessageType::TipRequest:
                {
                    TipSet request(error, stream);
                    if(!error)
                    {
                        LOG_TRACE(log) << "bootstrap_server::"<<__func__ <<" tip request parsed";
                        auto tip_server( std::make_shared<TipServer>(shared_from_this(), request, store));
                        tip_server->send_tips();
                    }
                    else
                        LOG_TRACE(log) << "bootstrap_server::"<<__func__ <<" tip request parse error";
                    break;
                }
                case MessageType::PullRequest:
                {
                    PullRequest pull(error, stream);
                    if(!error)
                    {
                        LOG_TRACE(log) << "bootstrap_server::"<<__func__ <<" pull request parsed";
                        auto pull_server( std::make_shared<PullServer>(shared_from_this(), pull, store));
                        pull_server->send_block();
                    }
                    else
                        LOG_TRACE(log) << "bootstrap_server::"<<__func__ <<" pull request parse error";
                    break;
                }
                default:
                    error = true;
                    break;
            }
        } else
            error = true;

        if(error)
            OnNetworkError();
    }

    std::shared_ptr<BootstrapServer> BootstrapServer::shared ()
    {
        return shared_from_base<BootstrapServer>();
    }
}
