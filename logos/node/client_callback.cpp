#include <logos/node/client_callback.hpp>

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

void BlocksCallback::SendMessage (std::shared_ptr<std::string> body)
{
    if (!_callback_address.empty ())
    {
        auto address (_callback_address);
        auto port (_callback_port);
        auto target (std::make_shared<std::string> (_callback_target));
        auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (_service));
        resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [this, address, port, target, body, resolver](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
            if (!ec)
            {
                for (auto i (i_a), n (boost::asio::ip::tcp::resolver::iterator{}); i != n; ++i)
                {
                    auto sock (std::make_shared<boost::asio::ip::tcp::socket> (_service));
                    sock->async_connect (i->endpoint (), [this, target, body, sock, address, port](boost::system::error_code const & ec) {
                        if (!ec)
                        {
                            auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
                            req->method (boost::beast::http::verb::post);
                            req->target (*target);
                            req->version (11);
                            req->insert (boost::beast::http::field::host, address);
                            req->insert (boost::beast::http::field::content_type, "application/json");
                            req->body () = *body;
                            req->prepare_payload ();
                            boost::beast::http::async_write (*sock, *req, [this, sock, address, port, req](boost::system::error_code const & ec, size_t bytes_transferred) {
                                if (!ec)
                                {
                                    auto sb (std::make_shared<boost::beast::flat_buffer> ());
                                    auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
                                    boost::beast::http::async_read (*sock, *sb, *resp, [this, sb, resp, sock, address, port](boost::system::error_code const & ec, size_t bytes_transferred) {
                                        if (!ec)
                                        {
                                            if (resp->result () == boost::beast::http::status::ok)
                                            {
                                            }
                                            else
                                            {
                                                if (_callback_logging)
                                                {
                                                    BOOST_LOG (_log) << boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ());
                                                }
                                            }
                                        }
                                        else
                                        {
                                            if (_callback_logging)
                                            {
                                                BOOST_LOG (_log) << boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ());
                                            }
                                        };
                                    });
                                }
                                else
                                {
                                    if (_callback_logging)
                                    {
                                        BOOST_LOG (_log) << boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ());
                                    }
                                }
                            });
                        }
                        else
                        {
                            if (_callback_logging)
                            {
                                BOOST_LOG (_log) << boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ());
                            }
                        }
                    });
                }
            }
            else
            {
                if (_callback_logging)
                {
                    BOOST_LOG (_log) << boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ());
                }
            }
        });
    }
}
