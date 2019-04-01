#pragma once

#include <future>

#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/lib/log.hpp>


namespace Bootstrap
{
	class ISocket;
    class tips_req_client : public std::enable_shared_from_this<Bootstrap::tips_req_client>
    {
    public:

    	/**
    	 * constructor
    	 * @param connection the connection to the peer
    	 * @param store the database
    	 */
        tips_req_client (std::shared_ptr<ISocket> connection, Store & store);

        /**
         * desctructor
         */
        ~tips_req_client ();

        /**
         * Start the tip request
         */
        void run ();

    private:
        friend class bootstrap_attempt;

        void receive_tips();

        std::shared_ptr<ISocket> connection;
        TipSet request;
        TipSet response;
        std::promise<bool> promise;
        Log log;
    };

    class tips_req_server : public std::enable_shared_from_this<Bootstrap::tips_req_server>
    {
    public:
        /**
         * constructor
         * @param connection the connection to the peer
         * @param request the tip request
         * @param store the database
         */
        tips_req_server (std::shared_ptr<ISocket> connection, TipSet & request, Store & store);

        /**
         * desctructor
         */
        ~tips_req_server();

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
