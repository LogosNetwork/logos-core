#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/utility.hpp>
#include <logos/lib/log.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>

#include <mutex>

class EpochInfo;
class IOChannelReconnect;
class NetIOErrorHandler;

class NetIOAssembler : public Self<NetIOAssembler>
{

    using Socket    = boost::asio::ip::tcp::socket;
    using ErrorCode = boost::system::error_code;

    using ReadCallback =
            std::function<void(const uint8_t * data)>;

public:

    NetIOAssembler(std::shared_ptr<Socket> socket);
    virtual ~NetIOAssembler() = default;

    void ReadPrequel(ReadCallback callback);
    void ReadBytes(ReadCallback callback, size_t bytes);

    void ResetSocket(std::shared_ptr<Socket> socket)
    {
        _socket = socket;
    }

protected:
    virtual void OnError(const ErrorCode &ec) {}
    virtual void OnRead() {}

    Log                            _log;

private:

    struct QueuedRequest
    {
        ReadCallback callback;
        size_t       bytes = 0;
    };

    static constexpr size_t BUFFER_CAPACITY = 1024000;

    using Buffer = std::array<uint8_t, BUFFER_CAPACITY>;

    void ReadBytes(ReadCallback cb, size_t bytes, bool read_in_progress);
    void AsyncRead();

    void OnData(const boost::system::error_code & error, size_t size);

    void ProcessCallback();
    void DoProcessCallback();
    void AdjustBuffer();

    bool Proceed(ReadCallback callback, size_t bytes);

    Buffer                         _buffer;
    ReadCallback                   _callback;
    QueuedRequest                  _queued_request;
    std::shared_ptr<Socket>        _socket;
    size_t                         _buffer_size         = 0;
    size_t                         _bytes_to_read       = 0;
    bool                           _processing_callback = false;
    atomic_bool                    _handled_error{false};
};

