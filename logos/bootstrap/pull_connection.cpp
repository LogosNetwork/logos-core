#include <logos/node/utility.hpp>
#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/pull_connection.hpp>

namespace Bootstrap
{

	bulk_pull_client::bulk_pull_client (std::shared_ptr<ISocket> connection, Puller & puller)
	: connection(connection)
	, puller(puller)
	, request(puller.GetPull())
	{
		LOG_TRACE(log) << __func__;
	}

	bulk_pull_client::~bulk_pull_client ()
	{
		LOG_TRACE(log) << __func__;
	}

	void bulk_pull_client::run()
	{
        LOG_TRACE(log) << "bulk_pull_client::run";
        auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
        {
            logos::vectorstream stream (send_buffer->data());
            request->Serialize(stream);
        }

        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer, [this_l, send_buffer](bool good)
        		{
					if (good)
					{
						LOG_TRACE(this_l->log) << "waiting peer tips...";
						this_l->receive_block();
					}
					else
					{
						LOG_TRACE(this_l->log) << "bulk_pull_client::run: net error";
						this_l->puller.PullFailed(request);
						this_l->connection->OnNetworkError();
					}
				});
	}

    void bulk_pull_client::receive_block ()
    {
        auto this_l = shared_from_this ();
        connection->AsyncReceive([this_l](bool good, MessageHeader header, uint8_t * buf)
        		{
        			Puller::PullStatus pull_status = Puller::PullStatus::Unknown;
        			if(good)
        	        {
        	            logos::bufferstream stream (buf, header.payload_size);
        	            switch (header.pull_response_ct) {
        	            case ConsensusType::BatchStateBlock:
        	            	pull_status = this_l->process_reply<ConsensusType::BatchStateBlock>(stream);
        	            	break;
        	            case ConsensusType::MicroBlock:
        	            	pull_status = this_l->process_reply<ConsensusType::MicroBlock>(stream);
        	            	break;
        	            case ConsensusType::Epoch:
        	            	pull_status = this_l->process_reply<ConsensusType::Epoch>(stream);
        	            	break;
        	            default:
        	            	break;
						}
        	        }

        			switch (pull_status) {
        			case Puller::PullStatus::Continue:
        				this_l->receive_block();
						break;
					case Puller::PullStatus::Done:
						this_l->connection->Release();
						this_l->connection = nullptr;
						break;
					case Puller::PullStatus::BlackListSender:
						this_l->connection->OnNetworkError(true);
						this_l->connection = nullptr;
						break;
					case Puller::PullStatus::DisconnectSender:
					default:
						this_l->connection->OnNetworkError();
						this_l->connection = nullptr;
						break;
					}
                });
    }

    /////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////

    bulk_pull_server::bulk_pull_server (std::shared_ptr<ISocket> server, PullPtr pull, Store & store)
    : connection(server)
    , request_handler(pull, store)
	{
		LOG_TRACE(log) << __func__;
	}

    bulk_pull_server::~bulk_pull_server()
	{
		LOG_TRACE(log) << __func__;
	}

    void bulk_pull_server::send_block ()
    {
        LOG_TRACE(log) << "bulk_pull_server::send_block";
        auto send_buffer(std::make_shared<std::vector<uint8_t>>());
        auto more (request_handler.GetNextSerializedResponse(*send_buffer));
        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer->data (), send_buffer->size (),
        		[this_l, more](bool good)
				{
					if (good)
					{
						LOG_TRACE(this_l->log) << "Sent a block";
						if(more)
						{
							this_l->send_block();
						}
						else
						{
							this_l->connection->Release();
							this_l->connection = nullptr;
						}
					}
					else
					{
						LOG_ERROR(this_l->log) << "Error sending tips";
						this_l->connection->OnNetworkError();
					}
				});
    }
}
