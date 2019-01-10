#include <logos/consensus/network/net_io_assembler.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>

NetIOAssembler::NetIOAssembler(std::shared_ptr<Socket> socket,
                               const std::atomic_bool & connected,
                               EpochInfo & epoch_info)
    : _socket(socket)
    , _connected(connected)
    , _epoch_info(epoch_info)
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
    LOG_DEBUG(_log) << "NetIOAssembler::ReadBytes - _buffer_size (" << _buffer_size
                    << ") 'bytes' size (" << bytes << "), processing callback" ;
    if(_buffer_size >= bytes)
    {
        LOG_DEBUG(_log) << "NetIOAssembler::ReadBytes - processing callback" ;
        ProcessCallback();
    }
    else
    {
        auto msg ("");
        if(!read_in_progress)
        {
            _bytes_to_read = bytes;
            _callback = callback;
            msg = "more ";
        }

        LOG_DEBUG(_log) << "NetIOAssembler::ReadBytes - async-reading " << msg << "data";
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
        if (_connected && !_epoch_info.IsWaitingDisconnect())
        {
            LOG_ERROR(_log) << "NetIOAssembler - Error receiving message: "
                            << error.message() << " global " << (int)DelegateIdentityManager::_global_delegate_idx
                            << " connection " << _epoch_info.GetConnectionName()
                            << " delegate " << _epoch_info.GetDelegateName()
                            << " state " << _epoch_info.GetStateName();
        }
        return;
    }

    _buffer_size += size;

    if(_buffer_size == BUFFER_CAPACITY)
    {
        LOG_ERROR(_log) << "NetIOAssembler: Buffer"
                        << " size has reached capacity.";
    }

    LOG_DEBUG(_log) << "NetIOAssembler::OnData - calling ReadBytes, read_in_progress true, "
                       "_buffer_size " << _buffer_size;
    ReadBytes(_callback, _bytes_to_read, true);
}

void NetIOAssembler::ProcessCallback()
{
    DoProcessCallback();
    AdjustBuffer();

    if((_bytes_to_read = _queued_request.bytes))
    {
        LOG_DEBUG(_log) << "NetIOAssembler::ProcessCallback - promoting queued request";
        _queued_request.bytes = 0;
        _callback = _queued_request.callback;

        ReadBytes(_callback, _bytes_to_read);
    }
}

void NetIOAssembler::DoProcessCallback()
{
    LOG_DEBUG(_log) << "NetIOAssembler::DoProcessCallback - flipping _processing_callback to true";
    _processing_callback = true;
    _callback(_buffer.data());
    LOG_DEBUG(_log) << "NetIOAssembler::DoProcessCallback - flipping _processing_callback to false";
    _processing_callback = false;
}

void NetIOAssembler::AdjustBuffer()
{
    memmove(_buffer.data(), _buffer.data() + _bytes_to_read,
            _buffer_size - _bytes_to_read);

    LOG_DEBUG(_log) << "NetIOAssembler::AdjustBuffer - _buffer_size (" << _buffer_size
                    << ") bytes to read (" << _bytes_to_read << ")";
    _buffer_size -= _bytes_to_read;
    _bytes_to_read = 0;
}

bool NetIOAssembler::Proceed(ReadCallback callback, size_t bytes)
{
    if(_processing_callback)
    {
        _queued_request.callback = callback;
        _queued_request.bytes = bytes;
        LOG_DEBUG(_log) << "NetIOAssembler::Proceed - _processing_callback true, queuing request";

        return false;
    }

    LOG_DEBUG(_log) << "NetIOAssembler::Proceed - _processing_callback false, proceeding";
    return true;
}
