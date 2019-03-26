// @file
// NetIOSend implements buffered boost async send on the same socket
//

#pragma once

#include <logos/lib/utility.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <mutex>
#include <vector>
#include <list>

/// Implements buffered async write.
/// Boost doesn't support concurrent async ops on the same socket.
class NetIOSend : Self<NetIOSend>
{
    using Socket        = boost::asio::ip::tcp::socket;
    using Error         = boost::system::error_code;
    using QueuedWrites  = std::list<std::shared_ptr<std::vector<uint8_t>>>;
public:
    /// Class constructor
    /// @param socket to write to
    NetIOSend(std::shared_ptr<Socket> socket = nullptr)
        : _socket(socket)
    {}
    virtual ~NetIOSend() = default;

    /// Send the buffer. The buffer ownership is passed to the function.
    /// @param buf to write [in]
    /// @return false if the socket is null, true otherwise
    bool AsyncSend(std::shared_ptr<std::vector<uint8_t>> buf);

    operator std::shared_ptr<Socket> () {return _socket;}

protected:
    /// Send queued data
    void AsyncSendBuffered();
    /// Reset the socket
    /// @param socket to reset [in]
    void Reset(std::shared_ptr<Socket> socket);
    /// Handle send error
    /// @param error [in]
    virtual void OnError(const Error &error) {}

    std::shared_ptr<Socket> _socket;                /// socket to send to
    std::mutex              _send_mutex;            /// protect concurrent writes
    QueuedWrites            _queued_writes;         /// data waiting to get sent on the network
    uint32_t                _queue_reservation = 0; /// how many queued entries are being sent
    bool                    _sending = false;       /// is an async write in progress
};
