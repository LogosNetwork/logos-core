#pragma once

#include <memory>

#include <logos/lib/log.hpp>
#include <logos/node/common.hpp>
#include <logos/node/node.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>

namespace Bootstrap
{
    constexpr uint16_t BOOTSTRAP_PORT = 7000;

    class ISocket
    {
    public:
        typedef void (*SendComplete)(bool good);
        typedef void (*ReceiveComplete)(bool good, MessageHeader header);

        virtual void AsyncSend(uint8_t * buf, size_t buf_len, SendComplete * cb, uint32_t timeout_ms = 0) = 0;
        virtual void AsyncReceive(uint8_t * buf, size_t buf_len, ReceiveComplete * cb, uint32_t timeout_ms = 0) = 0;
    };

    class Socket;
    class socket_timeout
    {
    public:
        /// Class constructor
        /// @param
        socket_timeout (Socket & socket);

        /// start start of timer
        /// @param time_point
        void start (std::chrono::steady_clock::time_point);

        /// stop stops the timer so as not to timeout
        void stop ();

    private:
        std::atomic<unsigned> ticket;
        Socket & socket;
    };

    class Socket : public ISocket, public std::enable_shared_from_this<Socket>
    {
    public:
        Socket(logos::tcp_endpoint & endpoint, std::shared_ptr<logos::alarm> alarm)
        : endpoint(endpoint)
        , alarm(alarm)
        , socket(alarm->service)
        , timeout (*this)
        {}

        void AsyncSend(uint8_t * buf, size_t buf_len, SendComplete * cb, uint32_t timeout_ms = 0) override
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

        void AsyncReceive(uint8_t * buf, size_t buf_len, ReceiveComplete * cb, uint32_t timeout_ms = 0) override
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

        std::shared_ptr<Socket> shared ()
        {
            return shared_from_this();
        }

        void disconnect()
        {
            socket.close ();
        }

        logos::tcp_endpoint endpoint;
        std::shared_ptr<logos::alarm> alarm;
        boost::asio::ip::tcp::socket socket;
        socket_timeout timeout;
        std::array<uint8_t, MessageHeader::WireSize> header_buf;
        Log log;
    };



    class bootstrap_attempt;
    class bootstrap_client : public std::enable_shared_from_this<bootstrap_client>
    {
    public:
        /// Class constructor
        /// @param node
        /// @param bootstrap_attempt
        /// @param tcp_endpoint
        bootstrap_client (std::shared_ptr<bootstrap_attempt>, logos::tcp_endpoint const &);

        /// Class destructor
        ~bootstrap_client ();

        /// run start of client
        void run ();

        /// shared
        /// @returns returns shared pointer of this client
        std::shared_ptr<bootstrap_client> shared ();

        /// start_timeout starts timeout on request
        void start_timeout ();
        /// stop_timeout called when request received
        void stop_timeout ();

        /// stop stops client
        /// @param force true if we are to force clients to finish instead of gradual shutdown
        void stop (bool force);

        std::shared_ptr<logos::alarm> alarm;
        std::shared_ptr<bootstrap_attempt> attempt;

        //logos::tcp_endpoint endpoint;
        //boost::asio::ip::tcp::socket socket;
        //socket_timeout timeout;

        std::array<uint8_t, BootstrapBufSize> receive_buffer;
        //std::chrono::steady_clock::time_point start_time;
        //std::atomic<uint64_t> block_count;
        //std::atomic<bool> pending_stop;
        //std::atomic<bool> hard_stop;
        Log log;
    };

    class bootstrap_server : public std::enable_shared_from_this<bootstrap_server>
    {
    public:
        /// Class constructor
        /// @param shared pointer of socket
        /// @param node
        bootstrap_server (std::shared_ptr<boost::asio::ip::tcp::socket>, Store & store);

        /// Class destructor
        ~bootstrap_server ();

        /// receive composed operation
        void receive ();

        /// receive_header_action composed operation
        /// @param error_code
        /// @param size
        void receive_header_action (boost::system::error_code const &, size_t);

        /// receive_bulk_pull_action composed operation
        /// @param error_code
        /// @param size
        void receive_pull_req_action (boost::system::error_code const &, size_t);

        /// receive_tips_req_action composed operation
        /// @param error_code
        /// @param size
        void receive_tips_req_action (boost::system::error_code const &, size_t);

        /// finish_request called to signal end of transmission by servers
        void finish_request ();

        /// run_next get the next work item to process
        void run_next ();

        std::array<uint8_t, BootstrapBufSize> receive_buffer;
        std::shared_ptr<boost::asio::ip::tcp::socket> socket;
        Store & store;
        std::mutex mutex;
        Log log;
    };

} //namespace
