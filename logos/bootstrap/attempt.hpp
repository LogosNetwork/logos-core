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
    class BootstrapClient;
    class BootstrapAttempt : public std::enable_shared_from_this<BootstrapAttempt>
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
        BootstrapAttempt(logos::alarm & alarm,
                Store & store,
                logos::BlockCache & cache,
                PeerInfoProvider &peer_provider,
                uint8_t max_connected);

        /**
         * destructor
         */
        ~BootstrapAttempt();

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
         * @param locked if the mutex protecting the connection lists is already locked
         * @return true if added (note that the connect operation is async)
         */
        bool add_connection(logos::endpoint const & endpoint, bool locked=false);

        /**
         * pool connection on idle list for re-use
         * @param client shared_ptr of the connection object
         * @param locked if the mutex protecting the connection lists is already locked
         */
        void pool_connection(std::shared_ptr<BootstrapClient> client, bool locked=false);

        /**
         * remove a connection from the lists of connections
         * @param client shared_ptr of the connection object
         * @param blacklist if the peer should be blacklisted
         */
        void remove_connection(std::shared_ptr<BootstrapClient> client, bool blacklist);

        logos::alarm & alarm;

        bool GetTipsets(TipSet &my_tips, TipSet &others_tips, uint8_t &mb_Qed, uint8_t &eb_Qed)
        {
            return puller->GetTipsets(my_tips, others_tips, mb_Qed, eb_Qed);
        }

        void wakeup();

    private:

        size_t target_connections(size_t need);

        bool populate_connections(size_t need = 0);

        std::shared_ptr<BootstrapClient> get_connection();

        bool request_tips();

        void request_pull();

        bool consume_future(std::future<bool> &);

        Store & store;
        PeerInfoProvider & peer_provider;

        std::mutex mtx;
        std::unordered_set<std::shared_ptr<BootstrapClient>> working_clients;
        std::unordered_set<std::shared_ptr<BootstrapClient>> idle_clients;
        std::unordered_set<std::shared_ptr<BootstrapClient>> connecting_clients;
        const uint8_t max_connected;
        int session_id;

        std::shared_ptr<Puller> puller;
        std::condition_variable condition;
        std::atomic<bool> stopped;
        Log log;
    };
}
