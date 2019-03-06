#pragma once

#include <logos/bootstrap/bootstrap_messages.hpp>
//#include <logos/bootstrap/bootstrap.hpp>
#include <logos/lib/log.hpp>

namespace Bootstrap
{
    class TipUtils{
    public:
        static TipSet CreateTipSet(Store & store)
        {
            logos::transaction transaction (store.environment, nullptr, false);

        }

        static bool IsBehind(const TipSet & a, const TipSet & b)
        {
            return true;
        }
    };

class tips_req_client : public std::enable_shared_from_this<Bootstrap::tips_req_client>
    {
    public:

        /// Class constructor
        /// @param bootstrap_client
        tips_req_client (std::shared_ptr<bootstrap_client> connection);

        /// Class destructor
        ~tips_req_client ();

        /// run starts the client
        void run ();

        /// receive_tips_header start of receiving the tips from server
        void receive_tips_header ();

        /// received_batch_block_tips final call to receiving tips in composed operation
        /// @param error_code error if there was a problem in the network
        /// @param size_t length of message received
        void received_batch_block_tips (boost::system::error_code const &, size_t);

        /// finish_request
        /// set promise and pool connection
        void finish_request();

        std::shared_ptr<bootstrap_client> connection;
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
        tips_req_server (std::shared_ptr<bootstrap_server> , TipSet);

        /// Class destructor
        ~tips_req_server();

        /// sends the batch block tips to the client
        void run();

        std::shared_ptr<bootstrap_server> connection;
        TipSet request;
        Log log;
    };
}