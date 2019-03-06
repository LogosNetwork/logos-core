#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/tips.hpp>
#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

namespace Bootstrap
{
    tips_req_client::tips_req_client (std::shared_ptr<bootstrap_client> connection)
    : connection (connection)
    {
        LOG_DEBUG(log) << __func__;
    }

    tips_req_client::~tips_req_client ()
    {
        LOG_DEBUG(log) << __func__;
    }

    void tips_req_client::run ()
    {
        LOG_DEBUG(log) << "tips_req_client::run";
        request = TipUtils::CreateTipSet(connection->attempt->store); // Get a snapshot of our tips.
        auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
        {
            logos::vectorstream stream (*send_buffer);
            request.Serialize(stream);
        }
        auto this_l = shared_from_this();
        connection->start_timeout ();
        boost::asio::async_write (connection->socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()),
              [this_l, send_buffer](boost::system::error_code const & ec, size_t size_a)
              {
                  this_l->connection->stop_timeout ();
                  if (!ec)
                  {
                      LOG_DEBUG(this_l->log) << "receive_tips_header:";
                      this_l->receive_tips_header ();
                  }
                  else
                  {
                      //auto address = connection->socket.remote_endpoint().address();
                      LOG_ERROR(this_l->log) << "Error while sending bootstrap request " << ec.message ();
                      try {
                          this_l->promise.set_value(true); // Report the error to caller in bootstrap.cpp.
                      } catch(const std::future_error &e)
                      {
                          LOG_DEBUG(this_l->log) << "tips_req_client::run: error setting promise: " << e.what();
                      }
                  }
              });
    }

    void tips_req_client::receive_tips_header ()
    {
        auto this_l = shared_from_this ();
        connection->start_timeout ();
        boost::asio::async_read (connection->socket,
                boost::asio::buffer (connection->receive_buffer.data (), MessageHeader::WireSize),
                [this_l](boost::system::error_code const & ec, size_t size_a)
                {
                    this_l->connection->stop_timeout ();
                    if (!ec)
                    {
                        logos::bufferstream stream (this_l->connection->receive_buffer.data (), MessageHeader::WireSize);
                        bool error = false;
                        MessageHeader header(error, stream);

                        if(error || ! header.Validate())
                        {
                            LOG_INFO(this_l->log) << "Header error";
                            //TODO disconnect?
                            return;
                        }

                        if(header.type == MessageType::TipResponse)
                        {
                            LOG_DEBUG(this_l->log) << "received_batch_block_tips header";
                            boost::asio::async_read (this_l->connection->socket,
                                    boost::asio::buffer(this_l->connection->receive_buffer.data(), header.payload_size),
                                    [this_l](boost::system::error_code const & ec, size_t size_a)
                                    {
                                        this_l->received_batch_block_tips(ec, size_a);
                                    });
                        } else {
                            LOG_DEBUG(this_l->log) << "error message type";
                            this_l->connection->stop(false); //TODO stop the client.
                            //TODO this_l->promise.set_value(true); //why not set promise?
                        }
                    }
                    else
                    {
                        try {
                            LOG_DEBUG(this_l->log) << "tips:: line: " << __LINE__ << " ec.message: " << ec.message();
                            this_l->promise.set_value(true);
                        }
                        catch(const std::future_error &e)
                        {
                            LOG_INFO(this_l->log) << "tips_req_client::receive_tips_header: caught error in setting promise: " << e.what();
                        }
                    }
                });
    }

    void tips_req_client::received_batch_block_tips(boost::system::error_code const &ec, size_t size_a)
    {
        LOG_DEBUG(log) << "tips_req_client::received_batch_block_tips: ec: " << ec;
        //auto address = connection->socket.remote_endpoint().address();

        if(!ec)
        {
            logos::bufferstream stream (connection->receive_buffer.data (), size_a);
            bool error = false;
            TipSet tips(error, stream);
            if (!error)
            {
                response = tips;
                finish_request();
            }
            //else disconnect
        } else {
            LOG_WARN(log) << "tips_req_client::received_batch_block_tips error...";
            //Error handling
        }
    }

    void tips_req_client::finish_request()
    {
        // Indicate we are done and all is well...
        try {
            promise.set_value(false); // We got everything, indicate we are ok to attempt
        }
        catch(const std::future_error &e)
        {
            LOG_DEBUG(log) << "tips_req_client::received_batch_block_tips caught error in setting promise: " << e.what();
        }
        connection->attempt->pool_connection (connection);
    }

    /////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////

    // NOTE Server sends tips to the client, client decides what to do
    tips_req_server::tips_req_server (std::shared_ptr<bootstrap_server> connection, TipSet & request)
    : connection (connection)
    , request (request)
    {
        LOG_DEBUG(log) << __func__;
    }

    tips_req_server::~tips_req_server()
    {
        LOG_DEBUG(log) << __func__;
    }

    void tips_req_server::run()
    {
        LOG_DEBUG(log) << "tips_req_server::run";
        //auto address = connection->socket->remote_endpoint().address();
        auto resp = TipUtils::CreateTipSet(connection->store);
        auto send_buffer(std::make_shared<std::vector<uint8_t>>());
        {
            logos::vectorstream stream(*send_buffer.get());
            resp.Serialize(stream);
        }

        LOG_DEBUG(log) << __func__ << " this: " << this << " connection: " << connection;
        auto this_l = shared_from_this();
        //TODO timeout
        boost::asio::async_write (*connection->socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()),
                                  [this_l](boost::system::error_code const & ec, size_t size_a)
                                  {
                                      if (!ec)
                                          LOG_INFO (this_l->log) << "Sending tips done";
                                      else
                                          LOG_ERROR(this_l->log) << "Error sending tips " << ec.message ());
                                  }

        //TODO compare tips and trigger local bootstrap
        //connection->node->ongoing_bootstrap();
    }

}//namespace

