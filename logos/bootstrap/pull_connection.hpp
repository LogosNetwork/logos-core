#pragma once


#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/pull.hpp>

namespace Bootstrap
{
    using PullPtr = std::shared_ptr<PullRequest>;

    class bulk_pull_client : public std::enable_shared_from_this<bulk_pull_client>
    {
    public:

    	/**
    	 * constructor
    	 * @param connection the connection to the peer
    	 * @param puller the puller generated the pull request this client will work on
    	 */
        bulk_pull_client (std::shared_ptr<ISocket> connection, Puller & puller);

        /**
         * desctructor
         */
        ~bulk_pull_client ();

        /**
         * Start the pull request
         */
        void run();

    private:

        void receive_block ();

        PullStatus process_reply (ConsensusType ct, logos::bufferstream & stream);

        std::shared_ptr<ISocket> connection;
        Puller & puller;
        PullPtr request;
        Log log;
    };

    class bulk_pull_server : public std::enable_shared_from_this<Bootstrap::bulk_pull_server>
    {
    public:
        /**
         * constructor
         * @param connection the connection to the peer
         * @param pull the pull request
         * @param store the database
         */
        bulk_pull_server (std::shared_ptr<ISocket> connection, PullRequest pull, Store & store);

        /**
         * desctructor
         */
        ~bulk_pull_server();

        /**
         * Start handling the pull request
         */
        void send_block ();

    private:

        std::shared_ptr<ISocket> connection;
        PullRequestHandler request_handler;
        Log log;
    };
}

