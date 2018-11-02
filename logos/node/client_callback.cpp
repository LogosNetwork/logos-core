#include <logos/node/client_callback.hpp>

std::shared_ptr<BlocksCallback> BlocksCallback::_instance = nullptr;

ClientCallback::ClientCallback(Service & service,
                               Log & log,
                               const std::string & callback_address,
                               const uint16_t & callback_port,
                               const std::string & callback_target,
                               const bool & callback_logging)
    : _service (service)
    , _log (log)
    , _callback_address (callback_address)
    , _callback_port (callback_port)
    , _callback_target (callback_target)
    , _callback_logging (callback_logging)
{}

BlocksCallback::BlocksCallback(Service & service,
                               Log & log,
                               const std::string & callback_address,
                               const uint16_t & callback_port,
                               const std::string & callback_target,
                               const bool & callback_logging)
        : ClientCallback (service, log, callback_address, callback_port, callback_target, callback_logging)
{}

std::shared_ptr<BlocksCallback> BlocksCallback::Instance(Service & service,
        Log & log,
        const std::string & callback_address,
        const uint16_t & callback_port,
        const std::string & callback_target,
        const bool & callback_logging)
{
    static std::shared_ptr<BlocksCallback> inst(new BlocksCallback(
            service, log, callback_address, callback_port, callback_target, callback_logging));
    if (BlocksCallback::_instance == nullptr)
    {
        BlocksCallback::_instance = inst;
    }
    return inst;
};

void BlocksCallback::SendMessage (std::shared_ptr<std::string> body)
{
    if (!_callback_address.empty ())
    {
        auto target (std::make_shared<std::string> (_callback_target));
        auto resolver (std::make_shared<TCP::resolver> (_service));
        resolver->async_resolve (TCP::resolver::query (_callback_address, std::to_string (_callback_port)), [this, target, body, resolver](boost::system::error_code const & ec, TCP::resolver::iterator i_a) {
            if (!ec)
            {
                for (auto i (i_a), n (TCP::resolver::iterator{}); i != n; ++i)
                {
                    auto sock (std::make_shared<TCP::socket> (_service));
                    sock->async_connect (i->endpoint (), [this, target, body, sock](boost::system::error_code const & ec) {
                        if (!ec)
                        {
                            auto req (std::make_shared<HTTP::request<HTTP::string_body>> ());
                            req->method (HTTP::verb::post);
                            req->target (*target);
                            req->version (11);
                            req->insert (HTTP::field::host, _callback_address);
                            req->insert (HTTP::field::content_type, "application/json");
                            req->body () = *body;
                            req->prepare_payload ();
                            HTTP::async_write (*sock, *req, [this, sock, req](boost::system::error_code const & ec, size_t bytes_transferred) {
                                if (!ec)
                                {
                                    auto sb (std::make_shared<boost::beast::flat_buffer> ());
                                    auto resp (std::make_shared<HTTP::response<HTTP::string_body>> ());
                                    HTTP::async_read (*sock, *sb, *resp, [this, sb, resp, sock](boost::system::error_code const & ec, size_t bytes_transferred) {
                                        if (!ec)
                                        {
                                            if (resp->result () == HTTP::status::ok)
                                            {
                                            }
                                            else
                                            {
                                                if (_callback_logging)
                                                {
                                                    BOOST_LOG (_log) << boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % _callback_address % _callback_port % resp->result ());
                                                }
                                            }
                                        }
                                        else
                                        {
                                            if (_callback_logging)
                                            {
                                                BOOST_LOG (_log) << boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % _callback_address % _callback_port % ec.message ());
                                            }
                                        };
                                    });
                                }
                                else
                                {
                                    if (_callback_logging)
                                    {
                                        BOOST_LOG (_log) << boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % _callback_address % _callback_port % ec.message ());
                                    }
                                }
                            });
                        }
                        else
                        {
                            if (_callback_logging)
                            {
                                BOOST_LOG (_log) << boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % _callback_address % _callback_port % ec.message ());
                            }
                        }
                    });
                }
            }
            else
            {
                if (_callback_logging)
                {
                    BOOST_LOG (_log) << boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % _callback_address % _callback_port % ec.message ());
                }
            }
        });
    }
}
