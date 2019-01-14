/// @file
/// This file contains the definition of the ClientCallback class and its derived classes,
/// which are used to send data to client observers

#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/utility.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

namespace // unnamed namespace to prevent visibility in other files
{
    namespace HTTP  = boost::beast::http;
}

class ClientCallback
{
protected:

    using Service     = boost::asio::io_service;
    using TCP         = boost::asio::ip::tcp;

public:
    ClientCallback(Service & service,
                   const std::string & callback_address,
                   const uint16_t & callback_port,
                   const std::string & callback_target,
                   const bool & callback_logging);

    virtual ~ClientCallback() {}

protected:
    Service &   _service;
    Log         _log;
    std::string _callback_address;
    uint16_t    _callback_port;
    std::string _callback_target;
    bool        _callback_logging;
};


class BlocksCallback : public ClientCallback
{
private:
    BlocksCallback(Service & service,
                   const std::string & callback_address,
                   const uint16_t & callback_port,
                   const std::string & callback_target,
                   const bool & callback_logging);

    static std::shared_ptr<BlocksCallback> _instance;

public:

    ~BlocksCallback() {}

    static std::shared_ptr<BlocksCallback>
    Instance(Service & service,
            const std::string & callback_address,
            const uint16_t & callback_port,
            const std::string & callback_target,
            const bool & callback_logging);

    void SendMessage (std::shared_ptr<std::string> body);

    template <ConsensusType CT>
    void NotifyClient (PrePrepareMessage<CT> block)
    { // implementation of non-specialized template needs to be visible to all usages
        _service.post([this, block] () {
            SendMessage(std::make_shared<std::string> (block.SerializeJson ()));
            //TODO question: should it be post-committed block?
        });
    }

    template <ConsensusType CT>
    static void Callback(PrePrepareMessage<CT> block)
    {
        _instance->NotifyClient(block);
    }

};
