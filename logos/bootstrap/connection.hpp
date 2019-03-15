#pragma once

#include <memory>

#include <logos/lib/log.hpp>
#include <logos/node/common.hpp>
#include <logos/node/node.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>

namespace Bootstrap
{
    constexpr uint16_t BOOTSTRAP_PORT = 7000;
    constexpr uint16_t CONNECT_TIMEOUT_MS = 5000; // 5 seconds

    using BoostSocket = boost::asio::ip::tcp::socket;

    class ISocket
    {
    public:
        typedef void (*SendComplete)(bool good);
        typedef void (*ReceiveComplete)(bool good, MessageHeader header, uint8_t * buf);

        virtual void AsyncSend(std::shared_ptr<std::vector<uint8_t>> buf, SendComplete * cb, uint32_t timeout_ms = 0) = 0;
        virtual void AsyncReceive(ReceiveComplete * cb, uint32_t timeout_ms = 0) = 0;
        virtual void OnNetworkError(bool black_list = false) = 0;
        virtual void Release() = 0;

        virtual ~ISocket() = default;
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
        //client side
        Socket(logos::tcp_endpoint & endpoint, logos::alarm & alarm);
        void Connect (std::function<void(bool)>);//void ConnectComplete(bool connected));

        //server side
        Socket(BoostSocket & socket_a, logos::alarm & alarm);

        virtual void AsyncSend(std::shared_ptr<std::vector<uint8_t>> buf, SendComplete * cb, uint32_t timeout_ms = 0) override;
        virtual void AsyncReceive(ReceiveComplete * cb, uint32_t timeout_ms = 0) override;

        void Disconnect();
        std::shared_ptr<Socket> shared ();

        logos::tcp_endpoint endpoint;
        logos::alarm & alarm;
        BoostSocket socket;
        socket_timeout timeout;
        std::array<uint8_t, MessageHeader::WireSize> header_buf;
        std::array<uint8_t, BootstrapBufSize> receive_buf;
        Log log;

    protected:
        template <typename Derived>
        std::shared_ptr<Derived> shared_from_base()
        {
            return std::static_pointer_cast<Derived>(shared_from_this());
        }
    };


    class bootstrap_attempt;
    class bootstrap_client : public Socket//std::enable_shared_from_this<bootstrap_client>
    {
    public:
        /// Class constructor
        /// @param node
        /// @param bootstrap_attempt
        /// @param tcp_endpoint
        bootstrap_client (bootstrap_attempt & attempt, logos::tcp_endpoint &);

        /// Class destructor
        ~bootstrap_client ();

        virtual void OnNetworkError(bool black_list = false) override;
        virtual void Release() override;

        void Disconnect ();

        /// @returns returns shared pointer of this client
        std::shared_ptr<bootstrap_client> shared ();

        bootstrap_attempt & attempt;
        //Store & store;
        Log log;
    };

    class bootstrap_listener;
    class bootstrap_server : public Socket
    {
    public:
        /// Class constructor
        /// @param shared pointer of socket
        /// @param node
        bootstrap_server (bootstrap_listener & listener,
                BoostSocket & socket_a,
                Store & store);

        /// Class destructor
        ~bootstrap_server ();

        void receive_request ();
        void dispatch (bool good, MessageHeader header, uint8_t * buf);

        virtual void OnNetworkError(bool black_list = false) override
        {
        	Socket::Disconnect();
        	//TODO notify attempt
        }
        virtual void Release() override;

        bootstrap_listener & listener;
        Store & store;
        //std::mutex mutex;
        Log log;
    };

} //namespace
