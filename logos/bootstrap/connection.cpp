#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/tip_connection.hpp>

namespace Bootstrap
{
    socket_timeout::socket_timeout (Socket & socket)
    : ticket (0)
    , socket (socket)
    {}

    void socket_timeout::start (std::chrono::steady_clock::time_point timeout_a)
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
                    LOG_DEBUG(socket_l->log) << "timeout, remote_endpoint " << socket_l->endpoint;
                }
            }
        });
    }

    void socket_timeout::stop ()
    {
        LOG_DEBUG(socket.log) << "socket_timeout::stop:";
        ++ticket;
    }

    /////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////

    using BoostError = boost::system::error_code;
    using BoostBuffer = boost::asio::buffer;

    Socket::Socket(logos::tcp_endpoint & endpoint, logos::alarm & alarm)
    : endpoint(endpoint)
    , alarm(alarm)
    , socket(alarm.service)
    , timeout (*this)
    {}

    Socket::Socket(boost::asio::ip::tcp::socket &socket_a, logos::alarm & alarm)
    : endpoint(socket_a.remote_endpoint())
    , alarm(alarm)
    , socket(std::move(socket_a))
    , timeout (*this)
    {}

    void Socket::Connect (void (*ConnectComplete)(bool connected))
    {
        LOG_TRACE(log) << __func__ << " this: " << this << " timeout_ms: " << CONNECT_TIMEOUT_MS;
        auto this_l(shared());
        timeout.start (std::chrono::steady_clock::now () + std::chrono::milliseconds(CONNECT_TIMEOUT_MS));
        socket.async_connect (endpoint, [this_l, ConnectComplete](BoostError const & ec)
        {
            this_l->timeout.stop();
            if (!ec)
            {
                LOG_TRACE(this_l->log) << "Socket::connect: connected";
                ConnectComplete(true);
            }
            else
            {
                LOG_ERROR(this_l->log) << "Socket::connect: network error: ec.message: " << ec.message();
                ConnectComplete(false);
            }
        });
    }

    void Socket::Disconnect()
    {
        socket.close ();
    }

    void Socket::AsyncSend(std::shared_ptr<std::vector<uint8_t>> buf, SendComplete * cb, uint32_t timeout_ms)
    {
        LOG_TRACE(log) << __func__ << " this: " << this << " timeout_ms: " << timeout_ms;
        auto this_l(shared());
        if(timeout_ms > 0) timeout.start (std::chrono::steady_clock::now () + std::chrono::milliseconds(timeout_ms));
        boost::asio::async_write (socket, BoostBuffer (buf->data(), buf->size()),
                [this_l, cb, timeout_ms](BoostError const & ec, size_t size_a)
                {
                    if(timeout_ms > 0) this_l->timeout.stop();
                    LOG_TRACE(this_l->log) << "Socket::sent data";
                    if (!ec)
                    {
                        (*cb)(true);
                    }else{
                        LOG_ERROR(this_l->log) << "Socket::Network error " << ec.message ();
                        (*cb)(false);
                        //this_l->Disconnect();//TODO cb should call OnNetworkError
                    }
                });
    }

    void Socket::AsyncReceive(ReceiveComplete * cb, uint32_t timeout_ms)
    {
        LOG_TRACE(log) << __func__ << " this: " << this << " timeout_ms: " << timeout_ms;
        auto this_l(shared());
        if(timeout_ms > 0) timeout.start (std::chrono::steady_clock::now () + std::chrono::milliseconds(timeout_ms));
        boost::asio::async_read (socket, BoostBuffer (header_buf.data (), MessageHeader::WireSize),
            [this_l, cb, timeout_ms](BoostError const & ec, size_t size_a)
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
                        (*cb)(false, header, nullptr);
                        //this_l->Disconnect();
                        return;
                    }

                    boost::asio::async_read (this_l->socket, BoostBuffer (this_l->receive_buf.data(), this_l->receive_buf.size()),
                        [this_l, cb, timeout_ms, header](BoostError const & ec, size_t size_a)
                        {
                            if(timeout_ms > 0) this_l->timeout.stop();
                            LOG_TRACE(this_l->log) << "Socket::received data";
                            if (!ec) {
                                assert (size_a == header.payload_size);
                                (*cb)(true, header, this_l->receive_buf.data());
                            }else{
                                LOG_ERROR(this_l->log) << "Socket::Network error " << ec.message ();
                                (*cb)(false, header, nullptr);
                                //this_l->Disconnect();
                                return;
                            }
                        });

                }else{
                    LOG_ERROR(this_l->log) << "Socket::Network error " << ec.message ();
                    MessageHeader header;
                    (*cb)(false, header, nullptr);
                    //this_l->Disconnect();
                    return;
                }
            });
    }

    std::shared_ptr<Socket> Socket::shared ()
    {
        return shared_from_this();
    }

    ///////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////

    bootstrap_client::bootstrap_client (std::shared_ptr<bootstrap_attempt> attempt_a,
            logos::tcp_endpoint & endpoint_a)
    : Socket (endpoint_a, attempt_a->alarm)
    , attempt (attempt_a)
    {
        auto this_l(shared());
        auto on_connected = [this_l](bool connected)
        {
            ++(this_l->attempt.connections);
            this_l->attempt.pool_connection (this_l);
        };
        Connect(on_connected);
    }

    bootstrap_client::~bootstrap_client ()
    {
        if(attempt)
            --attempt.connections;
        LOG_DEBUG(log) << "bootstrap_client::~bootstrap_client";
        Socket::Disconnect();
    }

    void bootstrap_client::Disconnect ()
    {
        LOG_DEBUG(log) << "bootstrap_client::Disconnect";
        Socket::Disconnect();
        OnNetworkError();
    }

	std::shared_ptr<bootstrap_client> bootstrap_client::shared ()
	{
		return reinterpret_cast<std::shared_ptr<bootstrap_client>>(shared_from_this());
	}

    ////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////

    bootstrap_server::bootstrap_server (bootstrap_listener & listener,
                                        BoostSocket & socket_a,
                                        Store & store)
    : Socket (socket_a, listener.alarm)
    , listener(listener)
    , store (store)
    {
        LOG_DEBUG(log) << "bootstrap_server::bootstrap_server";
    }

    bootstrap_server::~bootstrap_server ()
    {
        LOG_INFO (log) << "Exiting bootstrap server";
        Socket::Disconnect();//TODO check if can be call multiple times
    }

    void bootstrap_server::receive_request ()
    {
        LOG_DEBUG(log) << "bootstrap_server::receive";
        AsyncReceive(std::bind(&bootstrap_server::dispatch,
        		this, std::placeholders::_1, std::placeholders::_2));
    }

    void bootstrap_server::Release()
    {
    	receive_request ();
    }

    void bootstrap_server::dispatch (bool good, MessageHeader header, uint8_t * buf)
    {
        LOG_DEBUG(log) << "bootstrap_server::dispatch";
        bool error = false;
        if (good)
        {
            logos::bufferstream stream (buf, header.payload_size);
            switch (header.type)
            {
                case MessageType::TipRequest:
                    TipSet request(error, stream);
                    if(!error) {
                    	tips_req_server tip_server(shared_from_this(), request, store);
                    	tip_server.run();
                    }
                    break;
                case MessageType::PullRequest:
                    PullRequest pull(error, stream);
                    if(!error) {

                    }
                    break;
                default:
                    error = true;
                    break;
            }
        } else
            error = true;

        if(error)
            OnNetworkError();
    }
}
