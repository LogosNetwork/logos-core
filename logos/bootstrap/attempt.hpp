#pragma once

#include <logos/bootstrap/pull.hpp>
#include <logos/consensus/persistence/block_cache.hpp>

constexpr uint32_t bootstrap_tip_max_retry = 16;

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
        /// Class constructor
        /// @param node
        bootstrap_attempt(logos::alarm & alarm,
                Store & store,
                BlockCache & cache,
                PeerInfoProvider &peer_provider,
                uint8_t max_connected);

        /// Class destructor
        ~bootstrap_attempt();

        /// run start of bootstrap_attempt
        void run();
        /// stop stop the attempt
        void stop();

        /// add_connection Add an endpoint
        /// @param endpoint
        void add_connection(logos::endpoint const & endpoint);
        /// pool_connection store connection on idle queue for re-use
        void pool_connection(std::shared_ptr<bootstrap_client> client);
        void remove_connection(std::shared_ptr<bootstrap_client> client, bool blacklist);

        logos::alarm & alarm;
    private:
        /// @returns the number of connections to create
        size_t target_connections(size_t need);

        /// populate_connections create connections for pull requests
        bool populate_connections(size_t need = 0);

        /// connection
        /// @param unique_lock
        /// @returns a valid bootstrap_client
        std::shared_ptr<bootstrap_client> get_connection();//std::unique_lock<std::mutex> &);

        /// request_tips
        /// @param unique_lock
        /// @returns boolean
        bool request_tips();//std::unique_lock<std::mutex> &);

        /// request_pull
        /// @param unique_lock
        void request_pull();//std::unique_lock<std::mutex> &);

        /// consume_future
        /// @param future
        /// @returns boolean
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
