// @file
// This file declares TxAcceptorChannel which provides communication between standalone TxAcceptor
// and Delegate

#include <logos/consensus/tx_acceptor/tx_acceptor_channel.hpp>
#include <logos/node/node.hpp>

TxAcceptorChannel::TxAcceptorChannel(Service &service, const std::string & ip, const uint16_t port)
    : _service(service)
    , _endpoint(Endpoint(boost::asio::ip::make_address_v4(ip), port))
    , _delegate(service, _endpoint, *this)
    , _socket(nullptr)
{
}

void
TxAcceptorChannel::OnConnectionAccepted(const Endpoint endpoint, shared_ptr<Socket> socket)
{
    if (!Validate(endpoint, socket))
    {
        LOG_ERROR(_log) << "TxAcceptorChannel::OnConnectionAccepted failed to validate " << endpoint;
        socket->close();
        return;
    }

    std::lock_guard<std::mutex> lock(_send_mutex);
    // New connection from the delegate resets the socket
    _socket = socket;
}

// TODO implement buffered async send as class to use in NetIO
void
TxAcceptorChannel::SendQueue()
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

        _queued_writes.clear();
        boost::asio::async_write(*_socket,
                                 bufs,
                                 [this, bufs](const Error &ec, size_t size) {
            if (ec)
            {
                LOG_ERROR(_log) << "TxAcceptorChannel::OnSendRequest error " << ec.message();
                _socket.reset();
                return;
            }

            std::lock_guard<std::mutex> lock(_send_mutex);
            SendQueue();
       });
    }
}

logos::process_return
TxAcceptorChannel::OnSendRequest(std::shared_ptr<StateBlock> block, bool should_buffer)
{
    std::lock_guard<std::mutex> lock(_send_mutex);
    logos::process_return result{logos::process_result::initializing};

    if (nullptr == _socket)
    {
        return result;
    }

    auto buf{std::make_shared<vector<uint8_t>>()};
    block->Serialize(*buf);
    _queued_writes.push_back(buf);

    if (!_sending)
    {
        SendQueue();
    }

    // always return 'progress' in this case, we are not expecting response from the delegate
    result={logos::process_result::progress};

    return result;
}