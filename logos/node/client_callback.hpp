/// @file
/// This file contains the definition of the ClientCallback class and its derived classes,
/// which are used to send data to client observers

#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/utility.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>


class ClientCallback
{
protected:

    using Service     = boost::asio::io_service;
    using Log         = boost::log::sources::logger_mt;

public:
    ClientCallback(Service & service,
                   Log & log,
                   const std::string & callback_address,
                   const uint16_t & callback_port,
                   const std::string & callback_target,
                   const bool & callback_logging);

    virtual ~ClientCallback() {}

protected:
    Service &   _service;
    Log &       _log;
    std::string _callback_address;
    uint16_t    _callback_port;
    std::string _callback_target;
    bool        _callback_logging;
};


class BlocksCallback : public ClientCallback
{
private:
    BlocksCallback(Service & service,
                   Log & log,
                   const std::string & callback_address,
                   const uint16_t & callback_port,
                   const std::string & callback_target,
                   const bool & callback_logging);

public:
    static std::shared_ptr<BlocksCallback> _instance;

public:

    ~BlocksCallback() {}

    static std::shared_ptr<BlocksCallback>
    Instance(Service & service,
            Log & log,
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
        });
    }

    template <ConsensusType CT>
    static void Callback(PrePrepareMessage<CT> block)
    {
        _instance->NotifyClient(block);
    }

};
