#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/lib/log.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/* Boost v1.70 introduced breaking changes; the conditional compilation allows 1.6x to be supported as well. */
#if BOOST_VERSION < 107000
using socket_type = boost::asio::ip::tcp::socket;
#define beast_buffers boost::beast::buffers
#else
using socket_type = boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;
#define beast_buffers boost::beast::make_printable
#endif

namespace logos
{
class node;
namespace websocket
{
    constexpr uint16_t listener_port = 18000;
	class listener;
	class confirmation_options;

	/** Supported topics */
	enum class topic
	{
		invalid = 0,
		/** Acknowledgement of prior incoming message */
		ack,
		/** A confirmation message */
		confirmation,
		/** Auxiliary length, not a valid topic, must be the last enum */
		_length
	};
	constexpr size_t number_topics{ static_cast<size_t> (topic::_length) - static_cast<size_t> (topic::invalid) };

	/** A message queued for broadcasting */
	class message final
	{
	public:
		message (logos::websocket::topic topic_a) :
		topic (topic_a)
		{
		}
		message (logos::websocket::topic topic_a, boost::property_tree::ptree & tree_a) :
		topic (topic_a), contents (tree_a)
		{
		}

		std::shared_ptr<std::string> to_string () const;
		logos::websocket::topic topic;
		boost::property_tree::ptree contents;
	};

	class block_confirm_message_builder final
	{
	public:
        template<ConsensusType CT>
		message build (const PostCommittedBlock<CT> & block);
	};

	/** Options for subscriptions */
	class options
	{
	public:
		/**
		 * Checks if a message should be filtered for default options (no options given).
		 * @param message_a the message to be checked
		 * @return false - the message should always be broadcasted
		 */
		virtual bool should_filter (message const & message_a) const
		{
			return false;
		}
		virtual ~options () = default;
	};

	/**
	 * Options for block confirmation subscriptions
	 * Non-filtering options:
	 * - "include_block" (array of Consensus block type, default all types) - filter RequestBlock, MicroBlock, or EpochBlock
	 * Filtering options:
	 * - "accounts" (array of std::strings) - will only not filter blocks that have these accounts as source/destination
	 */
	struct confirmation_options final : public options
	{
	    confirmation_options () = default;
		confirmation_options (boost::property_tree::ptree const & options_a);
        bool interested(const ApprovedRB & block);
        bool interested(const ApprovedMB & block);
        bool interested(const ApprovedEB & block);

		bool include_rb{ true };
        bool include_mb{ true };
        bool include_eb{ true };
        //TODO account will supported later
		std::unordered_set<std::string> accounts;
	};

	/** A websocket session managing its own lifetime */
	class session final : public std::enable_shared_from_this<session>
	{
		friend class listener;

	public:
		/** Constructor that takes ownership over \p socket_a */
		explicit session (logos::websocket::listener & listener_a, socket_type socket_a);
		~session ();

		/** Perform Websocket handshake and start reading messages */
		void handshake ();

		/** Close the websocket and end the session */
		void close ();

		/** Read the next message. This implicitely handles incoming websocket pings. */
		void read ();

		/** Enqueue \p message_a for writing to the websockets */
		void write (logos::websocket::message message_a);

	private:
		/** The owning listener */
		logos::websocket::listener & ws_listener;
		/** Websocket */
		boost::beast::websocket::stream<socket_type> ws;
		/** Buffer for received messages */
		boost::beast::multi_buffer read_buffer;
		/** All websocket operations that are thread unsafe must go through a strand. */
		boost::asio::strand<boost::asio::io_context::executor_type> strand;
		/** Outgoing messages. The send queue is protected by accessing it only through the strand */
		std::deque<message> send_queue;

		/** Hash functor for topic enums */
		struct topic_hash
		{
			template <typename T>
			std::size_t operator() (T t) const
			{
				return static_cast<std::size_t> (t);
			}
		};
		/** Map of subscriptions -> options registered by this session. */
		std::unordered_map<topic, std::unique_ptr<options>, topic_hash> subscriptions;
		std::mutex subscriptions_mutex;

		/** Handle incoming message */
		void handle_message (boost::property_tree::ptree const & message_a);
		/** Acknowledge incoming message */
		void send_ack (std::string action_a, std::string id_a);
		/** Send all queued messages. This must be called from the write strand. */
		void write_queued_messages ();

		Log log;
	};

	/** Creates a new session for each incoming connection */
	class listener final : public std::enable_shared_from_this<listener>
	{
	public:
		listener (logos::node & node_a, std::string & local_address);

		/** Start accepting connections */
		void run ();
		void accept ();
		void on_accept (boost::system::error_code ec_a);

		/** Close all websocket sessions and stop listening for new connections */
		void stop ();

		/** Broadcast block confirmation. The content of the message depends on subscription options */
		template <ConsensusType CT>
		void broadcast_confirmation (const PostCommittedBlock<CT> & block);

		/** Broadcast \p message to all session subscribing to the message topic. */
		void broadcast (logos::websocket::message message_a);

		/**
		 * Per-topic subscribers check. Relies on all sessions correctly increasing and
		 * decreasing the subscriber counts themselves.
		 */
		bool any_subscriber (logos::websocket::topic const & topic_a) const
		{
			return subscriber_count (topic_a) > 0;
		}
		/** Getter for subscriber count of a specific topic*/
		size_t subscriber_count (logos::websocket::topic const & topic_a) const
		{
			return topic_subscriber_count[static_cast<std::size_t> (topic_a)];
		}

	private:
		/** A websocket session can increase and decrease subscription counts. */
		friend logos::websocket::session;

		/** Adds to subscription count of a specific topic*/
		void increase_subscriber_count (logos::websocket::topic const & topic_a);
		/** Removes from subscription count of a specific topic*/
		void decrease_subscriber_count (logos::websocket::topic const & topic_a);

		boost::asio::ip::tcp::acceptor acceptor;
		socket_type socket;
		std::mutex sessions_mutex;
		std::vector<std::weak_ptr<session>> sessions;
		std::array<std::atomic<std::size_t>, number_topics> topic_subscriber_count{};
		std::atomic<bool> stopped{ false };
		Log log;
	};
}
}

namespace logos_global
{
    template <ConsensusType CT>
    void OnNewBlock(const PostCommittedBlock<CT> & block);
}