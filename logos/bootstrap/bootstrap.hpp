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
#include <logos/consensus/consensus_p2p.hpp>


constexpr uint32_t bootstrap_tips_retry_limit = 16;
constexpr uint32_t bootstrap_max_retry = 64;

namespace logos
{
	class alarm;
}
namespace Bootstrap
{
    using Service = boost::asio::io_service;

    class bootstrap_attempt;
    class bootstrap_initiator
    {
    public:
        /// Class constructor
        /// @param node
        bootstrap_initiator (logos::alarm & alarm,
        		Store & store,
				BlockCache & cache,
				PeerInfoProvider & peer_provider,
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

        /// stop ends bootstrapping
        void stop ();

    private:
        Service & service;
        logos::alarm & alarm;//std::shared_ptr<logos::alarm> alarm;
		Store & store;
		BlockCache & cache;
		PeerInfoProvider & peer_provider;

        std::shared_ptr<bootstrap_attempt> attempt;
        bool stopped;
        std::mutex mutex;
        std::condition_variable condition;
        uint8_t max_connected;
        std::thread thread;
        Log log;
    };

    class bootstrap_server;
	class bootstrap_listener
	{
	public:
		/// Class constructor
		/// @param boost io_service
		/// @param node
		bootstrap_listener (logos::alarm & alarm,
				Store & store,
				std::string & local_address,
				//uint16_t port,
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
		void remove_connection(std::shared_ptr<bootstrap_server> server);

		boost::asio::ip::tcp::acceptor acceptor;
		logos::alarm & alarm;
		logos::tcp_endpoint local;
		Service & service;
		Store & store;
		uint8_t max_accepted;

		std::mutex mtx;
		std::unordered_set<std::shared_ptr<bootstrap_server>> connections;
		Log log;
	};
}
//        /// in_progress
//        /// @returns true if bootstrapping is running
//        bool in_progress ();
//        /// current_attempt
//        /// @returns shared pointer of the current bootstrap_attempt
//        std::shared_ptr<bootstrap_attempt> current_attempt ();
//
//    logos::tcp_endpoint endpoint ();
//void on_network_error();
