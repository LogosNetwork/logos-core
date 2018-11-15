#pragma once

#include <boost/asio/ip/tcp.hpp>

class DelegatePeerManager
{
protected:

    using Endpoint = boost::asio::ip::tcp::endpoint;
    using Socket   = boost::asio::ip::tcp::socket;

public:

    virtual ~DelegatePeerManager() = default;

    virtual void OnConnectionAccepted(const Endpoint endpoint, std::shared_ptr<Socket>) = 0;
};


