#pragma once


#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/pull.hpp>

namespace Bootstrap
{
    using PullPtr = std::shared_ptr<PullRequest>;

    class PullClient : public std::enable_shared_from_this<PullClient>
    {
    public:

        /**
         * constructor
         * @param connection the connection to the peer
         * @param puller the puller generated the pull request this client will work on
         */
        PullClient (std::shared_ptr<ISocket> connection, Puller & puller);

        /**
         * desctructor
         */
        ~PullClient ();

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

    class PullServer : public std::enable_shared_from_this<Bootstrap::PullServer>
    {
    public:
        /**
         * constructor
         * @param connection the connection to the peer
         * @param pull the pull request
         * @param store the database
         */
        PullServer (std::shared_ptr<ISocket> connection, PullRequest pull, Store & store);

        /**
         * desctructor
         */
        ~PullServer();

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

