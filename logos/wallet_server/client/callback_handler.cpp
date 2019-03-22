#include <logos/wallet_server/client/wallet_server_client.hpp>
#include <logos/wallet_server/client/callback_manager.hpp>
#include <logos/lib/blocks.hpp>

#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/format/free_funcs.hpp>

CallbackHandler::CallbackHandler(const ApprovedRB & block,
                                 const Endpoint callback_endpoint,
                                 Service & service,
                                 CallbackManager * manager,
                                 Handle handle)
    : _socket(service)
    , _callback_endpoint(callback_endpoint)
    , _block(block)
    , _manager(manager)
    , _handle(handle)
{
    _socket.async_connect(callback_endpoint,
                          std::bind(&CallbackHandler::OnConnect, this,
                          std::placeholders::_1));
}

void CallbackHandler::OnConnect(const boost::system::error_code & ec)
{
    if(ec)
    {
        LOG_ERROR(_log) << boost::str(boost::format("Unable to connect to callback address: %1%: %2%") % _callback_endpoint % ec.message());
        Done();

        return;
    }

    _request = std::make_shared<Request>();
    _request->method(boost::beast::http::verb::post);
    _request->target("/");
    _request->version(11);
    _request->insert(boost::beast::http::field::host, _callback_endpoint.address());
    _request->insert(boost::beast::http::field::content_type, "application/json");
    _request->body() = _block.ToJson();
    _request->prepare_payload();

    boost::beast::http::async_write(_socket, *_request,
                                    std::bind(&CallbackHandler::OnWrite, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));

}

void CallbackHandler::OnWrite(const boost::system::error_code & ec, size_t bytes)
{
    if(ec)
    {
        LOG_ERROR(_log) << boost::str(boost::format("Unable to send callback: %1%: %2%") % _callback_endpoint % ec.message());
        Done();

        return;
    }

    _request.reset();
    _buffer = std::make_shared<Buffer>();
    _response = std::make_shared<Response>();

    boost::beast::http::async_read(_socket, *_buffer, *_response,
                                   std::bind(&CallbackHandler::OnRead, this,
                                             std::placeholders::_1,
                                             std::placeholders::_2));
}

void CallbackHandler::OnRead(boost::system::error_code const & ec, size_t bytes)
{
    if(ec)
    {
        LOG_ERROR(_log) << boost::str(boost::format("Unable complete callback: %1%: %2%") % _callback_endpoint % ec.message());
        return;
    }

    if(_response->result() != boost::beast::http::status::ok)
    {
        LOG_ERROR(_log) << boost::str(boost::format("Callback to %1% failed with status: %2%") % _callback_endpoint % _response->result());
    }

    Done();
}

void CallbackHandler::Done()
{
    _manager->OnCallbackDone(_handle);
}
