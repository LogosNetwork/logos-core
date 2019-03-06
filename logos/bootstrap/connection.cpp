#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>

namespace Bootstrap
{
    socket_timeout::socket_timeout (Socket & socket)
    : ticket (0)
    , socket (socket)
    {}

    void socket_timeout::start (std::chrono::steady_clock::time_point timeout_a)
    {
        auto ticket_l (++ticket);
        std::weak_ptr<Socket> client_w (socket.shared ());
        socket.alarm->add (timeout_a, [client_w, ticket_l]()
        {
            // NOTE: If we timeout, we disconnect.
            if (auto client_l = client_w.lock ())
            {
                if (client_l->timeout.ticket == ticket_l)
                {
                    client_l->disconnect();
                    LOG_DEBUG(client_l->log) << "timeout, Disconnecting from "
                                             << client_l->socket.remote_endpoint ();
                }
            }
        });
    }

    void socket_timeout::stop ()
    {
        LOG_DEBUG(socket.log) << "socket_timeout::stop:";
        ++ticket;
    }


    Socket::Socket(logos::tcp_endpoint & endpoint, std::shared_ptr<logos::alarm> alarm)
    : endpoint(endpoint)
    , alarm(alarm)
    , socket(alarm->service)
    , timeout (*this)
    {}

    void Socket::AsyncSend(uint8_t * buf, size_t buf_len, SendComplete * cb, uint32_t timeout_ms = 0)
    {
        LOG_TRACE(log) << __func__ << " this: " << this << " timeout_ms: " << timeout_ms;
        auto this_l(shared());
        if(timeout_ms > 0) timeout.start (std::chrono::steady_clock::now () + std::chrono::milliseconds(timeout_ms));
        boost::asio::async_write (socket, boost::asio::buffer (buf, buf_len),
                [this_l, buf_len, cb, timeout_ms](boost::system::error_code const & ec, size_t size_a)
                {
                    if(timeout_ms > 0) this_l->timeout.stop();
                    LOG_TRACE(this_l->log) << "Socket::sent data";
                    if (!ec)
                    {
                        assert (size_a == buf_len);
                        cb(true);
                    }else{
                        LOG_ERROR(this_l->log) << "Socket::Network error " << ec.message ();
                        cb(false);
                        disconnect();
                    }
                }
    }

    void Socket::AsyncReceive(uint8_t * buf, size_t buf_len, ReceiveComplete * cb, uint32_t timeout_ms = 0)
    {
        LOG_TRACE(log) << __func__ << " this: " << this << " timeout_ms: " << timeout_ms;
        auto this_l(shared());
        if(timeout_ms > 0) timeout.start (std::chrono::steady_clock::now () + std::chrono::milliseconds(timeout_ms));
        boost::asio::async_read (socket, boost::asio::buffer (header_buf.data (), MessageHeader::WireSize),
            [this_l, buf, buf_len, cb, timeout_ms](boost::system::error_code const & ec, size_t size_a)
            {
                LOG_TRACE(this_l->log) << "Socket::received header data";
                if (!ec)
                {
                    assert (size_a == MessageHeader::WireSize);
                    logos::bufferstream stream (this_l->header_buf.data (), size_a);
                    bool error = false;
                    MessageHeader header(error, stream);
                    if(error || ! header.Validate())
                    {
                        LOG_ERROR(this_l->log) << "Socket::Header error";
                        cb(false, header);
                        disconnect();
                        return;
                    }

                    boost::asio::async_read (this_l->socket, boost::asio::buffer (buf, buf_len),
                        [this_l, buf, buf_len, cb, timeout_ms, header](boost::system::error_code const & ec, size_t size_a)
                        {
                            if(timeout_ms > 0) this_l->timeout.stop();
                            LOG_TRACE(this_l->log) << "Socket::received data";
                            if (!ec) {
                                assert (size_a == header.payload_size);
                                cb(true, header);
                            }else{
                                LOG_ERROR(this_l->log) << "Socket::Network error " << ec.message ();
                                cb(false, header);
                                disconnect();
                                return;
                            }
                        });

                }else{
                    LOG_ERROR(this_l->log) << "Socket::Network error " << ec.message ());
                    cb(false, header);
                    disconnect();
                    return;
                }
            }
    }

    ///////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////

    bootstrap_client::bootstrap_client (std::shared_ptr<bootstrap_attempt> attempt_a,
            logos::tcp_endpoint const & endpoint_a)
    : alarm (attempt_a->alarm)
    , attempt (attempt_a)
    , socket (attempt_a->alarm->service)
    , timeout (*this)
    , endpoint (endpoint_a)
    , pending_stop (false)
    , hard_stop (false)
    {
        ++attempt->connections; // NOTE: Number of connection attempts.
    }

    bootstrap_client::~bootstrap_client ()
    {
        if(attempt)
            --attempt->connections;
        LOG_DEBUG(log) << "bootstrap_client::~bootstrap_client";
        socket.close();
    }

    void bootstrap_client::stop (bool force)
    {
        LOG_DEBUG(log) << "bootstrap_client::stop:" << std::endl;
        pending_stop = true;
        if (force)
        {
            hard_stop = true;
        }
    }

    void bootstrap_client::start_timeout ()
    {
        timeout.start (std::chrono::steady_clock::now () + std::chrono::seconds (20)); // NOTE: Set a timeout
    }

    void bootstrap_client::stop_timeout ()
    {
        timeout.stop ();
    }

    void bootstrap_client::run ()
    {
        auto this_l (shared_from_this ());
        start_timeout ();
        socket.async_connect (endpoint, [this_l](boost::system::error_code const & ec)
        {
            // NOTE: endpoint is passed into the constructor of bootstrap_client, attempt to connect.
            this_l->stop_timeout ();
            if (!ec)
            {
                this_l->attempt->pool_connection (this_l); // NOTE: Add connection, updates idle queue.
            }
            else
            {
                LOG_DEBUG(this_l->log) << "bootstrap_client::run: network error: ec.message: " << ec.message();
            }
        });
    }

/*    std::shared_ptr<bootstrap_client> bootstrap_client::shared ()
    {
        return std::shared_from_this ();
    }*/

    ////////////////////////////////////////////////////////////////////////////////////////////

    bootstrap_server::bootstrap_server (std::shared_ptr<boost::asio::ip::tcp::socket> socket_a,
                                        Store & store)
            : socket (socket_a)
            , store (store)
    {
        LOG_DEBUG(log) << "bootstrap_server::bootstrap_server";
    }

    bootstrap_server::~bootstrap_server ()
    {
        LOG_INFO (log) << "Exiting bootstrap server";
        socket->close();

        //TODO
        //        std::lock_guard<std::mutex> lock (bootstrap.mutex);
        //        bootstrap.connections.erase (this);
    }

    void bootstrap_server::receive ()
    {
        LOG_DEBUG(log) << "bootstrap_server::receive";
        auto this_l (shared_from_this ());
        boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), MessageHeader::WireSize),
                [this_l](boost::system::error_code const & ec, size_t size_a)
                {
                    this_l->receive_header_action (ec, size_a);
                });
    }

    void bootstrap_server::receive_header_action (boost::system::error_code const & ec, size_t size_a)
    {
        // NOTE: Start of server request handling.
        LOG_DEBUG(log) << "bootstrap_server::receive_header_action";
        if (!ec)
        {
            //assert (size_a == MessageHeader::WireSize);
            logos::bufferstream stream (receive_buffer.data (), size_a);
            bool error = false;
            MessageHeader header(error, stream);
            if(error || ! header.Validate())
            {
                LOG_INFO(log) << "Header error";
                //TODO disconnect?
                return;
            }

            switch (header.type)
            {
                case MessageType::TipRequest:
                {
                    auto this_l (shared_from_this ());
                    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), header.payload_size),
                                [this_l](boost::system::error_code const & ec, size_t size_a)
                                {
                                    this_l->receive_tips_req_action (ec, size_a);
                                });
                    break;
                }
                case MessageType::PullRequest:
                {
                    auto this_l (shared_from_this ());
                    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), header.payload_size),
                            [this_l](boost::system::error_code const & ec, size_t size_a)
                            {
                                this_l->receive_pull_req_action (ec, size_a);
                            });
                    break;
                }
                default:
                    //TODO disconnect?
                    break;
            }
        }
        else
        {
            LOG_INFO(log) << "Error while receiving header "<<  ec.message ();
        }
    }

    void bootstrap_server::receive_pull_req_action (boost::system::error_code const & ec, size_t size_a)
    {
        LOG_DEBUG(log) << "bootstrap_server::receive_bulk_pull_action";
        if (!ec)
        {
            std::unique_ptr<logos::bulk_pull> request (new logos::bulk_pull);
            logos::bufferstream stream (receive_buffer.data (), size_a);
            auto error (request->deserialize (stream));
            if (!error)
            {
                add_request (std::unique_ptr<logos::message> (request.release ()));
                receive ();//TODO
            } else {
                LOG_ERROR(log) << "bootstrap_server::receive_bulk_pull_action:: error deserializing request";
                //TODO disconnect
            }
        }
    }

    void bootstrap_server::receive_tips_req_action (boost::system::error_code const & ec, size_t size_a)
    {
        LOG_DEBUG(log) << "bootstrap_server::receive_tips_req_action";
        if (!ec)
        {
            logos::bufferstream stream (receive_buffer.data (), size_a);
            bool error = false;
            TipSet request(error, stream);
            if (!error)
            {
                tips_req_server server(shared_from_this(), request);
                server.run();
                receive ();//TODO
            }
            //else//TODO disconnect
        }
        else
        {
            LOG_ERROR(log) << "Error sending receiving tips request: " << ec.message ();
            //TODO disconnect
        }
    }
}

