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
            logos::vectorstream stream (*send_buffer);
            request->Serialize(stream);
        }

        auto this_l = shared_from_this();
//        connection->AsyncSend(send_buffer, [this_l, send_buffer](bool good)
//        		{
//					if (good)
//					{
//						LOG_TRACE(this_l->log) << "waiting peer tips...";
//						this_l->receive_block();
//					}
//					else
//					{
//						LOG_TRACE(this_l->log) << "bulk_pull_client::run: net error";
//						this_l->puller.PullFailed(this_l->request);
//						this_l->connection->OnNetworkError();
//					}
//				});TODO
	}

    void bulk_pull_client::receive_block ()
    {
        auto this_l = shared_from_this ();
//        connection->AsyncReceive([this_l](bool good, MessageHeader header, uint8_t * buf)
//        		{
//        			PullStatus pull_status = PullStatus::Unknown;
//        			if(good)
//        	        {
//        	            logos::bufferstream stream (buf, header.payload_size);
//    	            	pull_status = this_l->process_reply(header.pull_response_ct, stream);
//        	        }
//
//        			switch (pull_status) {
//        			case PullStatus::Continue:
//        				this_l->receive_block();
//						break;
//					case PullStatus::Done:
//						this_l->connection->Release();
//						this_l->connection = nullptr;
//						break;
//					case PullStatus::BlackListSender:
//						this_l->connection->OnNetworkError(true);
//						this_l->connection = nullptr;
//						break;
//					case PullStatus::DisconnectSender:
//					default:
//						this_l->connection->OnNetworkError();
//						this_l->connection = nullptr;
//						break;
//					}
//                });TODO
    }

    PullStatus bulk_pull_client::process_reply (ConsensusType ct, logos::bufferstream & stream)
    {
    	bool error = false;
    	switch (ct) {
			case ConsensusType::BatchStateBlock:
			{
				PullResponse<ConsensusType::BatchStateBlock> response(error, stream);
				if(error || response.status == PullResponseStatus::NoBlock)
				{
					puller.PullFailed(request);
					return PullStatus::DisconnectSender;
				}
				return puller.BSBReceived(request, response.block,
								response.status == PullResponseStatus::LastBlock);
			}
			case ConsensusType::MicroBlock:
			{
				PullResponse<ConsensusType::MicroBlock> response(error, stream);
				if(error || response.status == PullResponseStatus::NoBlock)
				{
					puller.PullFailed(request);
					return PullStatus::DisconnectSender;
				}
				return puller.MBReceived(request, response.block);
			}
			case ConsensusType::Epoch:
			{
				PullResponse<ConsensusType::Epoch> response(error, stream);
				if(error || response.status == PullResponseStatus::NoBlock)
				{
					puller.PullFailed(request);
					return PullStatus::DisconnectSender;
				}
				return puller.EBReceived(request, response.block);
			}
			default:
				return PullStatus::Unknown;
		}
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
//        connection->AsyncSend(send_buffer->data (), send_buffer->size (),
//        		[this_l, more](bool good)
//				{
//					if (good)
//					{
//						LOG_TRACE(this_l->log) << "Sent a block";
//						if(more)
//						{
//							this_l->send_block();
//						}
//						else
//						{
//							this_l->connection->Release();
//							this_l->connection = nullptr;
//						}
//					}
//					else
//					{
//						LOG_ERROR(this_l->log) << "Error sending tips";
//						this_l->connection->OnNetworkError();
//					}
//				});TODO
    }
}
