#include <logos/node/utility.hpp>

#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/tips.hpp>
#include <logos/bootstrap/tip_connection.hpp>
#include <logos/bootstrap/connection.hpp>


namespace Bootstrap
{
    tips_req_client::tips_req_client (std::shared_ptr<ISocket> connection, Store & store)
    : connection (connection)
    , request(TipSet::CreateTipSet(store))
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
            logos::vectorstream stream (*send_buffer);
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
    /////////////////////////////////////////////////////////////////////////////////

    // NOTE Server sends tips to the client, client decides what to do
    tips_req_server::tips_req_server (std::shared_ptr<ISocket> connection, TipSet & request, Store & store)
    : connection (connection)
    , request (request)
    , response (TipSet::CreateTipSet(store))
    {
        LOG_TRACE(log) << __func__;
    }

    tips_req_server::~tips_req_server()
    {
        LOG_TRACE(log) << __func__;
    }

    void tips_req_server::send_tips()
    {
        LOG_TRACE(log) << "tips_req_server::run";
        auto send_buffer(std::make_shared<std::vector<uint8_t>>());
        {
            logos::vectorstream stream(*send_buffer);
            response.Serialize(stream);
        }

        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer, [this_l](bool good)
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
