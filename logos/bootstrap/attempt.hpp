#pragma once

#include <logos/bootstrap/pull.hpp>
#include <logos/consensus/persistence/block_cache.hpp>


namespace logos
{
	class alarm;
}
class PeerInfoProvider;

namespace Bootstrap
{
	class bootstrap_client;
	class bootstrap_attempt : public std::enable_shared_from_this<bootstrap_attempt>
    {
    public:
        /**
         * constructor
         * @param alarm for timers
         * @param store the database
         * @param cache the block cache
         * @param peer_provider the peer IP provider
         * @param max_connected the max number of connections
         */
        bootstrap_attempt(logos::alarm & alarm,
                Store & store,
                BlockCache & cache,
                PeerInfoProvider &peer_provider,
                uint8_t max_connected);

        /**
         * destructor
         */
        ~bootstrap_attempt();

        /**
         * start of bootstrap_attempt
         */
        void run();

        /**
         * stop the attempt
         */
        void stop();

        /**
         * try connect to an endpoint, if connected, pool the connection
         * @param endpoint contain the IP of the peer
         */
        void add_connection(logos::endpoint const & endpoint);

        /**
         * pool connection on idle list for re-use
         * @param client shared_ptr of the connection object
         * @param locked if the mutex protecting the idle list is already locked
         */
        void pool_connection(std::shared_ptr<bootstrap_client> client, bool locked=false);

        /**
         * remove a connection from the lists of connections
         * @param client shared_ptr of the connection object
         * @param blacklist if the peer should be blacklisted
         */
        void remove_connection(std::shared_ptr<bootstrap_client> client, bool blacklist);

        logos::alarm & alarm;
    private:

        size_t target_connections(size_t need);

        bool populate_connections(size_t need = 0);

        std::shared_ptr<bootstrap_client> get_connection();

        bool request_tips();

        void request_pull();

        bool consume_future(std::future<bool> &);

        Store & store;
        PeerInfoProvider & peer_provider;

        std::mutex mtx;
        std::unordered_set<std::shared_ptr<bootstrap_client>> working_clients;
        std::unordered_set<std::shared_ptr<bootstrap_client>> idle_clients;
        std::unordered_set<std::shared_ptr<bootstrap_client>> connecting_clients;
        const uint8_t max_connected;
        int session_id;

        Puller puller;
        std::condition_variable condition;
        std::atomic<bool> stopped;
        Log log;
    };
}
