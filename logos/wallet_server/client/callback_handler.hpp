#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/wallet_server/client/common.hpp>
#include <logos/lib/log.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http.hpp>

class CallbackManager;

class CallbackHandler
{

    using Service  = boost::asio::io_service;
    using Endpoint = boost::asio::ip::tcp::endpoint;
    using Socket   = boost::asio::ip::tcp::socket;
    using Request  = boost::beast::http::request<boost::beast::http::string_body>;
    using Response = boost::beast::http::response<boost::beast::http::string_body>;
    using Buffer   = boost::beast::flat_buffer;
    using Handle   = wallet_server::client::callback::Handle;

public:

    CallbackHandler(const ApprovedBSB & block,
                    const Endpoint callback_endpoint,
                    Service & service,
                    CallbackManager * manager,
                    Handle handle);

private:

    void OnConnect(const boost::system::error_code & ec);
    void OnWrite(const boost::system::error_code & ec, size_t bytes);
    void OnRead(boost::system::error_code const & ec, size_t bytes);

    // XXX: Do not use this object at all
    //      after calling this method.
    void Done();

    Socket                    _socket;
    Endpoint                  _callback_endpoint;
    ApprovedBSB               _block;
    std::shared_ptr<Request>  _request;
    std::shared_ptr<Buffer>   _buffer;
    std::shared_ptr<Response> _response;
    Log                       _log;
    CallbackManager *         _manager;
    Handle                    _handle;
};
