#include <logos/network/net_io_assembler.hpp>
#include <logos/consensus/messages/messages.hpp>

NetIOAssembler::NetIOAssembler(std::shared_ptr<Socket> socket)
    : _socket(socket)
{}

void NetIOAssembler::ReadPrequel(ReadCallback callback)
{
    ReadBytes(callback, MessagePrequelSize);
}

void NetIOAssembler::ReadBytes(ReadCallback callback, size_t bytes)
{
    if(Proceed(callback, bytes))
    {
        ReadBytes(callback, bytes, false);
    }
}

void NetIOAssembler::ReadBytes(ReadCallback callback, size_t bytes, bool read_in_progress)
{
    if(_buffer_size >= bytes)
    {
        OnRead();
        ProcessCallback();
    }
    else
    {
        if(!read_in_progress)
        {
            _bytes_to_read = bytes;
            _callback = callback;
        }

        AsyncRead();
    }
}

void NetIOAssembler::AsyncRead()
{
    std::weak_ptr<NetIOAssembler> this_w = shared_from_this();
    boost::asio::async_read(*_socket,
                            boost::asio::buffer(_buffer.data() + _buffer_size,
                                                BUFFER_CAPACITY - _buffer_size),
                            boost::asio::transfer_at_least(1), [this_w](const ErrorCode &ec, size_t size) {
                                auto this_s = GetSharedPtr(this_w, "NetIOAssembler::AsyncRead, object destroyed");
                                if (!this_s)
                                {
                                    return;
                                }
                                this_s->OnData(ec, size);
                            });
}

void NetIOAssembler::OnData(const boost::system::error_code & error, size_t size)
{
    if (_handled_error)
    {
        return;
    }

    if(error)
    {
        _handled_error = true;
        OnError(error);
        return;
    }

    _buffer_size += size;

    if(_buffer_size == BUFFER_CAPACITY)
    {
        LOG_ERROR(_log) << "NetIOAssembler: Buffer"
                        << " size has reached capacity.";
    }

    ReadBytes(_callback, _bytes_to_read, true);
}

void NetIOAssembler::ProcessCallback()
{
    DoProcessCallback();
    AdjustBuffer();

    if((_bytes_to_read = _queued_request.bytes))
    {
        _queued_request.bytes = 0;
        _callback = _queued_request.callback;

        ReadBytes(_callback, _bytes_to_read);
    }
}

void NetIOAssembler::DoProcessCallback()
{
    _processing_callback = true;
    _callback(_buffer.data());
    _processing_callback = false;
}

void NetIOAssembler::AdjustBuffer()
{
    memmove(_buffer.data(), _buffer.data() + _bytes_to_read,
            _buffer_size - _bytes_to_read);

    _buffer_size -= _bytes_to_read;
    _bytes_to_read = 0;
}

bool NetIOAssembler::Proceed(ReadCallback callback, size_t bytes)
{
    if(_processing_callback)
    {
        _queued_request.callback = callback;
        _queued_request.bytes = bytes;

        return false;
    }

    return true;
}
