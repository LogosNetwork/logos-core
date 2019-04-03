#pragma once

#include <future>

#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/lib/log.hpp>


namespace Bootstrap
{
	class ISocket;
    class TipClient : public std::enable_shared_from_this<Bootstrap::TipClient>
    {
    public:

    	/**
    	 * constructor
    	 * @param connection the connection to the peer
    	 * @param store the database
    	 */
        TipClient (std::shared_ptr<ISocket> connection, Store & store);

        /**
         * desctructor
         */
        ~TipClient ();

        /**
         * Start the tip request
         */
        void run ();

    private:
        friend class BootstrapAttempt;

        void receive_tips();

        std::shared_ptr<ISocket> connection;
        TipSet request;
        TipSet response;
        std::promise<bool> promise;
        Log log;
    };

    class TipServer : public std::enable_shared_from_this<Bootstrap::TipServer>
    {
    public:
        /**
         * constructor
         * @param connection the connection to the peer
         * @param request the tip request
         * @param store the database
         */
        TipServer (std::shared_ptr<ISocket> connection, TipSet & request, Store & store);

        /**
         * desctructor
         */
        ~TipServer();

        /**
         * Start handling the tip request
         */
        void send_tips();

    private:

        std::shared_ptr<ISocket> connection;
        TipSet request;
        TipSet response;
        Log log;
    };
}
