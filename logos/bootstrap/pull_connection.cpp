#include <logos/node/utility.hpp>
#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/pull_connection.hpp>

namespace Bootstrap
{

	PullClient::PullClient (std::shared_ptr<ISocket> connection, Puller & puller)
	: connection(connection)
	, puller(puller)
	, request(puller.GetPull())
	{
		LOG_TRACE(log) << "bulk_pull_client::"<<__func__;
	}

	PullClient::~PullClient ()
	{
		LOG_TRACE(log) << "bulk_pull_client::"<<__func__;
	}

	void PullClient::run()
	{
        LOG_TRACE(log) << "bulk_pull_client::run";
        auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
        {
            logos::vectorstream stream (*send_buffer);
            MessageHeader header(logos_version, MessageType::PullRequest,
            		ConsensusType::Any, PullRequest::WireSize);
            header.Serialize(stream);
            request->Serialize(stream);
        }

        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer, [this, this_l, send_buffer](bool good)
        		{
					if (good)
					{
						receive_block();
					}
					else
					{
						LOG_TRACE(log) << "bulk_pull_client::run: net error";
						puller.PullFailed(request);
						connection->OnNetworkError();
					}
				});
	}

    void PullClient::receive_block ()
    {
		LOG_TRACE(log) << "bulk_pull_client::"<<__func__<< ": waiting peer blocks...";
        auto this_l = shared_from_this ();
        connection->AsyncReceive([this, this_l](bool good, MessageHeader header, uint8_t * buf)
        		{
        			LOG_TRACE(log) << "bulk_pull_client::"<<__func__ <<" good="<<good;
        			PullStatus pull_status = PullStatus::Unknown;
        			if(good)
        	        {
#ifdef DUMP_BLOCK_DATA
{
	std::stringstream stream;
	for(size_t i = 0; i < header.payload_size; ++i)
	{
		stream << std::hex << std::noshowbase << std::setw (2) << std::setfill ('0') << (unsigned int)(buf[i]);
	}
	LOG_TRACE(log) << "bulk_pull_client::"<<__func__ <<"::data:" << stream.str ();
}
#endif
        	            logos::bufferstream stream (buf, header.payload_size);
    	            	pull_status = process_reply(header.pull_response_ct, stream);
        	        }

        			switch (pull_status) {
        			case PullStatus::Continue:
#ifdef BOOTSTRAP_PROGRESS
        				block_progressed();
#endif
        				receive_block();
						break;
					case PullStatus::Done:
#ifdef BOOTSTRAP_PROGRESS
						block_progressed();
#endif
						connection->Release();
						connection = nullptr;
						break;
					case PullStatus::BlackListSender:
						connection->OnNetworkError(true);
						connection = nullptr;
						break;
					case PullStatus::DisconnectSender:
					default:
						connection->OnNetworkError();
						connection = nullptr;
						break;
					}
                });
    }

    PullStatus PullClient::process_reply (ConsensusType ct, logos::bufferstream & stream)
    {
		LOG_TRACE(log) << "bulk_pull_client::"<<__func__;
    	bool error = false;
    	switch (ct) {
			case ConsensusType::Request:
			{
				PullResponse<ConsensusType::Request> response(error, stream);
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

    PullServer::PullServer (std::shared_ptr<ISocket> connection, PullRequest pull, Store & store)
    : connection(connection)
    , request_handler(pull, store)
	{
		LOG_TRACE(log) << "bulk_pull_server::"<<__func__ << " " << pull.to_string();
	}

    PullServer::~PullServer()
	{
		LOG_TRACE(log) << "bulk_pull_server::"<<__func__;
	}

    void PullServer::send_block ()
    {
		LOG_TRACE(log) << "bulk_pull_server::"<<__func__;
        auto send_buffer(std::make_shared<std::vector<uint8_t>>());
        auto more (request_handler.GetNextSerializedResponse(*send_buffer));

#ifdef DUMP_BLOCK_DATA
{
	std::stringstream stream;
	uint8_t * buf = send_buffer->data();
	for(size_t i = MessageHeader::WireSize; i < send_buffer->size(); ++i)
	{
		stream << std::hex << std::noshowbase << std::setw (2) << std::setfill ('0') << (unsigned int)(buf[i]);
	}
	LOG_TRACE(log) << "bulk_pull_server::"<<__func__ <<"::data:" << stream.str ();
}
#endif

        auto this_l = shared_from_this();
        connection->AsyncSend(send_buffer, [this, this_l, more](bool good)
				{
					if (good)
					{
						LOG_TRACE(log) << "bulk_pull_server::send_block: Sent a block";
						if(more)
						{
							send_block();
						}
						else
						{
							connection->Release();
							connection = nullptr;
						}
					}
					else
					{
						LOG_ERROR(log) << "Error sending tips";
						connection->OnNetworkError();
					}
				});
    }
}
