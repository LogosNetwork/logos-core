#pragma once

#include <atomic>
#include <future>
#include <queue>
#include <stack>
#include <unordered_set>

#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>
#include <logos/node/common.hpp>
#include <logos/consensus/persistence/block_cache.hpp>
#include <logos/node/peer_provider.hpp>

namespace logos
{
    class alarm;
}
namespace Bootstrap
{
    constexpr uint8_t max_out_connection = 32;
    constexpr uint8_t max_accept_connection = 64;

    using Service = boost::asio::io_service;

    class BootstrapAttempt;
    class BootstrapInitiator
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
        BootstrapInitiator (logos::alarm & alarm,
                Store & store,
                logos::BlockCache & cache,
                PeerInfoProvider & peer_provider,
                uint8_t max_connected = max_out_connection);

        /**
         * desctructor
         */
        ~BootstrapInitiator ();

        /**
         * create bootstrap attempt and try to bootstrap from the peer
         * @param peer endpoint to bootstrap from
         */
        void bootstrap (logos::endpoint const & peer);

        /**
         * create bootstrap attempt
         */
        void bootstrap ();

        /**
         * starting function of the dedicated bootstrap kick off thread
         */
        void run_bootstrap ();

        /**
         * check progress of an on-going attempt
         * note that an attempt with bad progress is stopped
         * @return true if bootstrapping is running and have good progress
         */
        bool check_progress ();

        /**
         * end client side bootstrapping
         */
        void stop ();

        bool GetTipsets(TipSet &my_tips, TipSet &others_tips);

    private:
        Service & service;
        logos::alarm & alarm;
        Store & store;
        logos::BlockCache & cache;
        PeerInfoProvider & peer_provider;

        std::shared_ptr<BootstrapAttempt> attempt;
        bool stopped;
        bool one_more;
        std::mutex mtx;
        std::condition_variable condition;
        uint8_t max_connected;
        Log log;

        std::thread thread;
    };

    class BootstrapServer;
    class BootstrapListener
    {
    public:
        /**
         * constructor
         * @param alarm for timers
         * @param store the database
         * @param local_address address of the local node
         * @param max_accepted the max number of connections will be accepted
         */
        BootstrapListener (logos::alarm & alarm,
                Store & store,
                std::string & local_address,
                uint8_t max_accepted = max_accept_connection);

        /**
         * destructor
         */
        ~BootstrapListener();

        /**
         * start listening for connection requests
         */
        void start ();

        /**
         * end server side bootstrap
         */
        void stop ();

        /**
         * remove a connection from the list of connections
         * @param server shared_ptr of the connection object
         */
        void remove_connection(std::shared_ptr<BootstrapServer> server);

        logos::alarm & alarm;

    private:

        void accept_connection ();

        void accept_action (boost::system::error_code const &,
                std::shared_ptr<boost::asio::ip::tcp::socket>);


        boost::asio::ip::tcp::acceptor acceptor;
        logos::tcp_endpoint local;
        Service & service;
        Store & store;
        uint8_t max_accepted;

        std::mutex mtx;
        std::condition_variable condition;
        std::unordered_set<std::shared_ptr<BootstrapServer>> connections;
        Log log;
    };
}
