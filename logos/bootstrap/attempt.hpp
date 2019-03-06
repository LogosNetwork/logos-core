#pragma once

#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/bulk_pull.hpp>

namespace Bootstrap {

class bootstrap_attempt : public std::enable_shared_from_this<Bootstrap::bootstrap_attempt>
    {
    public:
        /// Class constructor
        /// @param node
        bootstrap_attempt(std::shared_ptr<logos::alarm> alarm,
                Store & store,
                BlockCache & cache,
                PeerInfoProvider &peer_provider,
                uint8_t max_connected);

        /// Class destructor
        ~bootstrap_attempt();

        /// run start of bootstrap_attempt
        void run();

        /// @returns the number of connections to create
        size_t target_connections(size_t need);

        /// populate_connections create connections for pull requests
        void populate_connections(size_t need = 0);

        /// add_connection Add an endpoint
        /// @param endpoint
        void add_connection(logos::endpoint const &);

        /// pool_connection store connection on idle queue for re-use
        void pool_connection(std::shared_ptr<bootstrap_client>);

        /// connection
        /// @param unique_lock
        /// @returns a valid bootstrap_client
        std::shared_ptr<bootstrap_client> connection(std::unique_lock<std::mutex> &);

        /// request_tips
        /// @param unique_lock
        /// @returns boolean
        bool request_tips(std::unique_lock<std::mutex> &);

        /// request_pull
        /// @param unique_lock
        void request_pull(std::unique_lock<std::mutex> &);

        /// consume_future
        /// @param future
        /// @returns boolean
        bool consume_future(std::future<bool> &);

        /// stop stop the attempt
        void stop();

        void clean();


        std::shared_ptr<logos::alarm> alarm;
        Store & store;
        PeerInfoProvider &peer_provider;

        std::mutex mutex;
        std::deque<std::shared_ptr<bootstrap_client>> working_clients;
        //std::shared_ptr<bootstrap_client> connection_tips;
        std::deque<std::shared_ptr<bootstrap_client>> idle_clients;
        std::atomic<unsigned> connections;
        uint8_t max_connected;
        int session_id;

        Puller puller;
        std::condition_variable condition;
        bool stopped;
        Log log;
    };
}