#pragma once

#include <atomic>
#include <future>
#include <queue>
#include <stack>
#include <unordered_set>

#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>
#include <logos/node/common.hpp>

#include <logos/bootstrap/attempt.hpp>
#include <logos/bootstrap/connection.hpp>

constexpr double bootstrap_connection_scale_target_blocks = 50000.0;
constexpr double bootstrap_connection_warmup_time_sec = 5.0;
constexpr double bootstrap_minimum_blocks_per_sec = 10.0;
constexpr double bootstrap_minimum_termination_time_sec = 30.0;
constexpr uint32_t bootstrap_max_new_connections = 10;//128; // NOTE: Increase limit from 10.
constexpr uint32_t bootstrap_tips_retry_limit = 16;
constexpr uint32_t bootstrap_max_retry = 64;

namespace Bootstrap
{
    using Service = boost::asio::io_service;

    class bootstrap_initiator
    {
    public:
        /// Class constructor
        /// @param node
        bootstrap_initiator (Service & service,
                logos::alarm & alarm,
                uint8_t max_connected = 8);

        /// Class desctructor
        ~bootstrap_initiator ();

        /// bootstrap
        /// @param endpoint to bootstrap from
        /// @param add_to_peers add this endpoint to list of peers to bootstrap from
        void bootstrap (logos::endpoint const &);

        /// bootstrap initiates bootstrapping
        void bootstrap ();

        /// run_bootstrap start of bootstrapping
        void run_bootstrap ();

        /// in_progress
        /// @returns true if bootstrapping is running
        bool in_progress ();

        /// current_attempt
        /// @returns shared pointer of the current bootstrap_attempt
        std::shared_ptr<bootstrap_attempt> current_attempt ();

        /// stop ends bootstrapping
        void stop ();

    private:
        Service & service;
        logos::alarm & alarm;//std::shared_ptr<logos::alarm> alarm;
        std::shared_ptr<bootstrap_attempt> attempt;
        bool stopped;
        std::mutex mutex;
        std::condition_variable condition;
        uint8_t max_connected;
        std::thread thread;
        Log log;
    };

class bootstrap_listener
{
public:
    /// Class constructor
    /// @param boost io_service 
    /// @param node
    bootstrap_listener (Service & service,
            logos::alarm & alarm,
            Store & store,
            std::string & local_address,
            uint16_t port,
            uint8_t max_accepted = 16);

    /// start beginning of listener
    void start ();

    /// stop end of listener
    void stop ();

    /// accept_connection
    void accept_connection ();

    /// accept_action handles the server socket accept
    /// @param error_code
    /// @param shared pointer of socket
    void accept_action (boost::system::error_code const &,
            std::shared_ptr<boost::asio::ip::tcp::socket>);

    logos::tcp_endpoint endpoint ();

    void on_network_error();

    boost::asio::ip::tcp::acceptor acceptor;
    std::shared_ptr<logos::alarm> alarm;
    logos::tcp_endpoint local;
    Service & service;
    Store & store;
    uint8_t max_accepted;

    std::mutex mutex;
    std::unordered_map<bootstrap_server *, std::weak_ptr<bootstrap_server>> connections;
    bool on;
    Log log;
};

}
