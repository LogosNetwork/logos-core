#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/tips.hpp>
#include <logos/bootstrap/tip_connection.hpp>


namespace Bootstrap
{
    tips_req_client::tips_req_client (std::shared_ptr<ISocket> connection, Store & store)
    : connection (connection)
    , request(TipUtils::CreateTipSet(store))
    {
        LOG_TRACE(log) << __func__;
    }

    tips_req_client::~tips_req_client ()
    {
    	 LOG_TRACE(log) << __func__;
    }

    void tips_req_client::run ()
    {
        LOG_TRACE(log) << "tips_req_client::run";
        auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
        {
            logos::vectorstream stream (send_buffer->data());
            request.Serialize(stream);
        }

        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer, [this_l, send_buffer](bool good)
        		{
					if (good)
					{
						LOG_TRACE(this_l->log) << "waiting peer tips...";
						this_l->receive_tips();
					}
					else
					{
						try {
							this_l->promise.set_value(true); // Report the error to caller in bootstrap.cpp.
						}
						catch(const std::future_error &e)
						{
							LOG_TRACE(this_l->log) << "tips_req_client::run: error setting promise: " << e.what();
						}
					}
				});
    }

    void tips_req_client::receive_tips()
    {
        auto this_l = shared_from_this ();
        connection->AsyncReceive([this_l](bool good, MessageHeader header, uint8_t * buf)
        		{
        			bool error = false;
        			if(good)
        	        {
        	            logos::bufferstream stream (buf, header.payload_size);
        	            new (&this_l->response) TipSet(error, stream);
        	            if (!error)
        	            {
        	            	this_l->connection->Release();
        	            	this_l->connection = nullptr;
        	            	this_l->promise.set_value(false);
        	            }
        	        } else {
        	            LOG_WARN(this_l->log) << "tips_req_client::received_tips error...";
        	            error = true;
        	        }

        			if(error)
        			{
        				this_l->connection->OnNetworkError();
        				this_l->connection = nullptr;
        				this_l->promise.set_value(true);
        			}
                });
    }


    /////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////

    // NOTE Server sends tips to the client, client decides what to do
    tips_req_server::tips_req_server (std::shared_ptr<ISocket> connection, TipSet & request, Store & store)
    : connection (connection)
    , request (request)
    , response (TipUtils::CreateTipSet(store))
    {
        LOG_TRACE(log) << __func__;
    }

    tips_req_server::~tips_req_server()
    {
        LOG_TRACE(log) << __func__;
    }

    void tips_req_server::run()
    {
        LOG_TRACE(log) << "tips_req_server::run";
        auto send_buffer(std::make_shared<std::vector<uint8_t>>());
        {
            logos::vectorstream stream(send_buffer->data());
            response.Serialize(stream);
        }

        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer->data (), send_buffer->size (),
        		[this_l](bool good)
				{
        			if (good)
        			{
        				LOG_INFO (this_l->log) << "Sending tips done";
        				this_l->connection->Release();
        			}
        			else
        			{
        				LOG_ERROR(this_l->log) << "Error sending tips";
        				this_l->connection->OnNetworkError();
        			}

				});

        //TODO compare tips and trigger local bootstrap
        //connection->node->ongoing_bootstrap();
    }

}//namespace

//                }
//
////        	if (!ec)
////                    {
////                        logos::bufferstream stream (this_l->connection->receive_buffer.data (), MessageHeader::WireSize);
////                        bool error = false;
////                        MessageHeader header(error, stream);
////
////                        if(error || ! header.Validate())
////                        {
////                            LOG_INFO(this_l->log) << "Header error";
////                            //TODO disconnect?
////                            return;
////                        }
////
////                        if(header.type == MessageType::TipResponse)
////                        {
////                            LOG_DEBUG(this_l->log) << "received_batch_block_tips header";
////                            boost::asio::async_read (this_l->connection->socket,
////                                    boost::asio::buffer(this_l->connection->receive_buffer.data(), header.payload_size),
////                                    [this_l](boost::system::error_code const & ec, size_t size_a)
////                                    {
////                                        this_l->received_batch_block_tips(ec, size_a);
////                                    });
////                        } else {
////                            LOG_DEBUG(this_l->log) << "error message type";
////                            this_l->connection->stop(false); //TODO stop the client.
////                            //TODO this_l->promise.set_value(true); //why not set promise?
////                        }
////                    }
////                    else
////                    {
////                        try {
////                            LOG_DEBUG(this_l->log) << "tips:: line: " << __LINE__ << " ec.message: " << ec.message();
////                            this_l->promise.set_value(true);
////                        }
////                        catch(const std::future_error &e)
////                        {
////                            LOG_INFO(this_l->log) << "tips_req_client::receive_tips_header: caught error in setting promise: " << e.what();
////                        }
////                    }
////                });
//    }
//
//    void tips_req_client::received_batch_block_tips(boost::system::error_code const &ec, size_t size_a)
//    {
//        LOG_DEBUG(log) << "tips_req_client::received_batch_block_tips: ec: " << ec;
//        //auto address = connection->socket.remote_endpoint().address();
//
//        if(!ec)
//        {
//            logos::bufferstream stream (connection->receive_buffer.data (), size_a);
//            bool error = false;
//            TipSet tips(error, stream);
//            if (!error)
//            {
//                response = tips;
//                finish_request();
//            }
//            //else disconnect
//        } else {
//            LOG_WARN(log) << "tips_req_client::received_batch_block_tips error...";
//            //Error handling
//        }
//    }
//
//    void tips_req_client::finish_request()
//    {
//        // Indicate we are done and all is well...
//        try {
//            promise.set_value(false); // We got everything, indicate we are ok to attempt
//        }
//        catch(const std::future_error &e)
//        {
//            LOG_DEBUG(log) << "tips_req_client::received_batch_block_tips caught error in setting promise: " << e.what();
//        }
//        connection->attempt->pool_connection (connection);
//    }
