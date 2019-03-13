// @file
// NetIOSend implements buffered boost async send on the same socket
//
#include <logos/network/net_io_send.hpp>
#include <boost/asio/write.hpp>
#include <logos/lib/log.hpp>

bool
NetIOSend::AsyncSend(std::shared_ptr<std::vector<uint8_t>> buf)
{
    std::lock_guard<std::mutex> lock(_send_mutex);

    if (nullptr == _socket)
    {
        return false;
    }

    _queued_writes.push_back(buf);

    if (!_sending)
    {
        AsyncSendBuffered();
    }

    return true;
}

void
NetIOSend::Reset(std::shared_ptr<Socket> socket)
{
    std::lock_guard<std::mutex> lock(_send_mutex);
    _socket = socket;
}

void
NetIOSend::AsyncSendBuffered()
{
    auto begin = _queued_writes.begin();
    auto end = _queued_writes.begin();
    std::advance(end, _queue_reservation);

    _queued_writes.erase(begin, end);

    _queue_reservation = _queued_writes.size();

    _sending = false;

    if (_queue_reservation)
    {
        _sending = true;
        std::vector<boost::asio::const_buffer> bufs;

        for (auto b: _queued_writes)
        {
            bufs.push_back(boost::asio::buffer(b->data(), b->size()));
        }

        boost::asio::async_write(*_socket,
                                 bufs,
                                 [this, bufs](const Error &ec, size_t size) {
            if (ec)
            {
                OnError(ec);
                return;
            }

            std::lock_guard<std::mutex> lock(_send_mutex);
                                     AsyncSendBuffered();
       });
    }
}
