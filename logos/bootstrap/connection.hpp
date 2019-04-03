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
    constexpr uint32_t timeout_disabled = 0;

    using BoostSocket = boost::asio::ip::tcp::socket;

    /**
     * Interface of a network endpoint used in bootstrap
     */
    class ISocket
    {
    public:
    	using SendComplete = std::function<void(bool)>;
    	using ReceiveComplete = std::function<void(bool, MessageHeader, uint8_t *)>;

    	/**
    	 * asynchronously send data to the connected peer
    	 * @param buf the buffer of data
    	 * @param cb the callback that will be call when the send completes
    	 * @param timeout_ms timeout of the operation in ms
    	 */
        virtual void AsyncSend(std::shared_ptr<std::vector<uint8_t>> buf, SendComplete cb, uint32_t timeout_ms = timeout_disabled) = 0;

        /**
         * asynchronously receive data sent by the connected peer
         * @param cb the callback that will be call when the receive completes
         * @param timeout_ms timeout of the operation in ms
         */
        virtual void AsyncReceive(ReceiveComplete cb, uint32_t timeout_ms = timeout_disabled) = 0;

        /**
         * called when the connection has any kind of error, eg. data received cannot be parsed
         * @param black_list if the connected peer should be blacklisted
         */
        virtual void OnNetworkError(bool black_list = false) = 0;

        /**
         * release the connect after use
         */
        virtual void Release() = 0;

        /*
         * destructor
         */
        virtual ~ISocket() = default;
    };

    class Socket;
    /**
     * the object that keeps track if an asynchronous operation has timed out.
     * if so, the connection will be disconnected.
     */
    class SocketTimeout
    {
    public:
    	/**
    	 * constructor
    	 * @param socket the connection object what will be used for async operations
    	 */
    	SocketTimeout (Socket & socket);

        /**
         * start the timer
         * @param time_point the time that the timeout event will be triggered
         */
        void start (std::chrono::steady_clock::time_point);

        /**
         *  stop the timer so as not to timeout
         */
        void stop ();

    private:
        std::atomic<unsigned> ticket;
        Socket & socket;
    };

    /**
     * The connection endpoint
     */
    class Socket : public ISocket, public std::enable_shared_from_this<Socket>
    {
    public:
    	/**
    	 * client side constructor
    	 * @param endpoint the peer to connect to
    	 * @param alarm for timers
    	 */
        Socket(logos::tcp_endpoint & endpoint, logos::alarm & alarm);

        /**
         * server side constructor
         * @param socket_a the connected low level socket
         * @param alarm for timers
         */
        Socket(BoostSocket & socket_a, logos::alarm & alarm);

        /**
         * connect to the peer
         * @param ConnectComplete the connection completion callback
         */
        void Connect (std::function<void(bool)> ConnectComplete);

        /**
         * disconnect the connection
         */
        void Disconnect();

        /**
         * destructor
         */
        virtual ~Socket() override;

    	/**
    	 * (inherited) asynchronously send data to the connected peer
    	 * @param buf the buffer of data
    	 * @param cb the callback that will be call when the send completes
    	 * @param timeout_ms timeout of the operation in ms
    	 */
        virtual void AsyncSend(std::shared_ptr<std::vector<uint8_t>> buf, SendComplete cb, uint32_t timeout_ms = timeout_disabled) override;

        /**
         * (inherited) asynchronously receive data sent by the connected peer
         * @param cb the callback that will be call when the receive completes
         * @param timeout_ms timeout of the operation in ms
         */
        virtual void AsyncReceive(ReceiveComplete cb, uint32_t timeout_ms = timeout_disabled) override;

        /**
         * get the shared_ptr of self
         * @return the shared_ptr of self
         */
        std::shared_ptr<Socket> shared ();

        /**
         * get the address of the connected peer
         * @return the address of the connected peer
         */
        boost::asio::ip::address PeerAddress() const;

    protected:
        template <typename Derived>
        std::shared_ptr<Derived> shared_from_base()
        {
            return std::static_pointer_cast<Derived>(shared_from_this());
        }

        logos::tcp_endpoint peer;
        logos::alarm & alarm;
        BoostSocket socket;
        SocketTimeout timeout;
        std::array<uint8_t, MessageHeader::WireSize> header_buf;
        std::array<uint8_t, BootstrapBufSize> receive_buf;

        std::mutex mtx;
        bool disconnected;
        Log log;

        friend class SocketTimeout;
    };


    class BootstrapAttempt;
    /**
     * the client side connection object
     */
    class BootstrapClient : public Socket
    {
    public:
    	/**
    	 * constructor
    	 * @param attempt the bootstrap attempt constructing this object
    	 * @param peer the peer's IP address
    	 */
    	BootstrapClient (BootstrapAttempt & attempt, logos::tcp_endpoint & peer);

        /**
         * destructor
         */
        ~BootstrapClient ();

        /**
          * (inherited) called when the connection has any kind of error,
          * eg. data received cannot be parsed
          * @param black_list if the connected peer should be blacklisted
          */
        virtual void OnNetworkError(bool black_list = false) override;

        /**
         * (inherited) release the connect after use
         */
        virtual void Release() override;

        /**
         * get the shared_ptr of this object
         * @return shared_ptr of this object
         */
        std::shared_ptr<BootstrapClient> shared ();

    private:
        BootstrapAttempt & attempt;
        Log log;
    };

    class BootstrapListener;
    class BootstrapServer : public Socket
    {
    public:
        /**
         * constructor
         * @param listener the listener constructing this object
         * @param socket_a the connected low level socket
         * @param store the database
         */
        BootstrapServer (BootstrapListener & listener,
                BoostSocket & socket_a,
                Store & store);

        /**
         * destructor
         */
        ~BootstrapServer ();

        /**
         * waiting to receive peer's request
         */
        void receive_request ();

        /**
          * (inherited) called when the connection has any kind of error,
          * eg. data received cannot be parsed
          * @param black_list if the connected peer should be blacklisted
          */
        virtual void OnNetworkError(bool black_list = false) override;

        /**
         * (inherited) release the connect after use
         */
        virtual void Release() override;

        /**
         * get the shared_ptr of this object
         * @return shared_ptr of this object
         */
        std::shared_ptr<BootstrapServer> shared ();

    private:

        void dispatch (bool good, MessageHeader header, uint8_t * buf);

        BootstrapListener & listener;
        Store & store;
        Log log;
    };

} //namespace
