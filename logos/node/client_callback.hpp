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

public:
    BlocksCallback(Service & service,
                   Log & log,
                   const std::string & callback_address,
                   const uint16_t & callback_port,
                   const std::string & callback_target,
                   const bool & callback_logging);

    void SendMessage (std::shared_ptr<std::string> body);

    void NotifyClient (BatchStateBlock block);
    void NotifyClient (MicroBlock block);
    void NotifyClient (Epoch block);

};
