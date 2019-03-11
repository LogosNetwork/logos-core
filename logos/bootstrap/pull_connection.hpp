#pragma once


#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/pull.hpp>

namespace Bootstrap
{
    using PullPtr = std::shared_ptr<PullRequest>;

    class bulk_pull_client : public std::enable_shared_from_this<Bootstrap::bulk_pull_client>
    {
    public:
        /// Class constructor
        /// @param bootstrap_client
        /// @param pull_info
        bulk_pull_client (std::shared_ptr<ISocket> connection, Puller & puller);

        /// Class desctructor
        ~bulk_pull_client ();

        /// request_batch_block start of operation
        void run();

        /// receive_block composed operation
        void receive_block ();

        template<ConsensusType CT>
        Puller::PullStatus process_reply (logos::bufferstream & stream)
        {
        	bool error = false;
        	PullResponse<CT> response(error, stream);
        	if (!error)
        	{
        		return puller.BlockReceived(request,
        				response.block_number,
						response.block,
						response.status==PullResponseStatus::LastBlock);
        	}
        	else
        	{
        		puller.PullFailed(request);
        	}

        	return Puller::PullStatus::Unknown;
        }

        std::shared_ptr<ISocket> connection;
        Puller & puller;
        PullPtr request;
        Log log;
    };

    class bulk_pull;
    class bulk_pull_server : public std::enable_shared_from_this<Bootstrap::bulk_pull_server>
    {
    public:
        /// Class constructor
        /// @param bootstrap_server
        /// @param bulk_pull (the actual request being made)
        bulk_pull_server (std::shared_ptr<ISocket> server, PullPtr pull, Store & store);

        ~bulk_pull_server();

        void send_block ();

        std::shared_ptr<ISocket> connection;
        PullRequestHandler request_handler;
        Log log;
    };

    //template Puller::PullStatus process_reply<ConsensusType::BatchStateBlock>(logos::bufferstream & stream);
}
