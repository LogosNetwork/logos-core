#pragma once

#include <future>

#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/lib/log.hpp>


namespace Bootstrap
{
    class tips_req_client : public std::enable_shared_from_this<Bootstrap::tips_req_client>
    {
    public:

        /// Class constructor
        /// @param bootstrap_client
        tips_req_client (std::shared_ptr<ISocket> connection, Store & store);

        /// Class destructor
        ~tips_req_client ();

        /// run starts the client
        void run ();

        /// receive_tips receive the tips from server
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

        /// Class constructor
        /// @param bootstrap_server
        /// @param frontier_req (request made by the client)
        tips_req_server (std::shared_ptr<ISocket> connection, TipSet request, Store & store);

        /// Class destructor
        ~tips_req_server();

        /// sends the tips to the client
        void send_tips();

        std::shared_ptr<ISocket> connection;
        TipSet request;
        TipSet response;
        Log log;
    };
}

//        /// received_batch_block_tips final call to receiving tips in composed operation
//        /// @param error_code error if there was a problem in the network
//        /// @param size_t length of message received
//        void received_batch_block_tips (boost::system::error_code const &, size_t);
//
//        /// finish_request
//        /// set promise and pool connection
//        void finish_request();

