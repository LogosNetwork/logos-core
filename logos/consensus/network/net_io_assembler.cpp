#include <logos/consensus/network/net_io_assembler.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/node/node_identity_manager.hpp>

#include <boost/log/sources/record_ostream.hpp>

NetIOAssembler::NetIOAssembler(std::shared_ptr<Socket> socket,
                               const std::atomic_bool & connected,
                               const ConnectingDelegatesSet & delegates_set)
    : _socket(socket)
    , _connected(connected)
    , _delegates_set(delegates_set)
{}

void NetIOAssembler::ReadPrequel(ReadCallback callback)
{
    ReadBytes(callback, sizeof(Prequel));
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
    boost::asio::async_read(*_socket,
                            boost::asio::buffer(_buffer.data() + _buffer_size,
                                                BUFFER_CAPACITY - _buffer_size),
                            boost::asio::transfer_at_least(1),
                            std::bind(&NetIOAssembler::OnData, this,
                                      std::placeholders::_1,
                                      std::placeholders::_2));
}

void NetIOAssembler::OnData(const boost::system::error_code & error, size_t size)
{
    if(error)
    {
        // cancelled at the end of epoch transition
        if (_connected && _delegates_set != ConnectingDelegatesSet::Outgoing)
        {
            BOOST_LOG(_log) << "NetIOAssembler - Error receiving message: "
                            << error.message() << " global " << (int)NodeIdentityManager::_global_delegate_idx
                            << " delegates set " << DelegatesSetToName(_delegates_set);
        }
        return;
    }

    _buffer_size += size;

    if(_buffer_size == BUFFER_CAPACITY)
    {
        BOOST_LOG(_log) << "NetIOAssembler - Error: Buffer"
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
