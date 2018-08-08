#pragma once

#include <logos/blockstore.hpp>
#include <logos/ledger.hpp>
#include <logos/node/common.hpp>

#include <atomic>
#include <future>
#include <queue>
#include <stack>
#include <unordered_set>

#include <boost/log/sources/logger.hpp>

namespace logos
{
class bootstrap_attempt;
class bootstrap_client;
class node;
enum class sync_result
{
	success,
	error,
	fork
};
class socket_timeout
{
public:
	socket_timeout (logos::bootstrap_client &);
	void start (std::chrono::steady_clock::time_point);
	void stop ();

private:
	std::atomic<unsigned> ticket;
	logos::bootstrap_client & client;
};

/**
 * The length of every message header, parsed by logos::message::read_header ()
 * The 2 here represents the size of a std::bitset<16>, which is 2 chars long normally
 */
static const int bootstrap_message_header_size = sizeof (logos::message::magic_number) + sizeof (uint8_t) + sizeof (uint8_t) + sizeof (uint8_t) + sizeof (logos::message_type) + 2;

class bootstrap_client;
class pull_info
{
public:
	pull_info ();
	pull_info (logos::account const &, logos::block_hash const &, logos::block_hash const &);
	logos::account account;
	logos::block_hash head;
	logos::block_hash end;
	unsigned attempts;
};
class frontier_req_client;
class bulk_push_client;
class bootstrap_attempt : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	bootstrap_attempt (std::shared_ptr<logos::node> node_a);
	~bootstrap_attempt ();
	void run ();
	std::shared_ptr<logos::bootstrap_client> connection (std::unique_lock<std::mutex> &);
	bool consume_future (std::future<bool> &);
	void populate_connections ();
	bool request_frontier (std::unique_lock<std::mutex> &);
	void request_pull (std::unique_lock<std::mutex> &);
	void request_push (std::unique_lock<std::mutex> &);
	void add_connection (logos::endpoint const &);
	void pool_connection (std::shared_ptr<logos::bootstrap_client>);
	void stop ();
	void requeue_pull (logos::pull_info const &);
	void add_pull (logos::pull_info const &);
	bool still_pulling ();
	void process_fork (MDB_txn *, std::shared_ptr<logos::block>);
	unsigned target_connections (size_t pulls_remaining);
	bool should_log ();
	void add_bulk_push_target (logos::block_hash const &, logos::block_hash const &);
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::weak_ptr<logos::bootstrap_client>> clients;
	std::weak_ptr<logos::bootstrap_client> connection_frontier_request;
	std::weak_ptr<logos::frontier_req_client> frontiers;
	std::weak_ptr<logos::bulk_push_client> push;
	std::deque<logos::pull_info> pulls;
	std::deque<std::shared_ptr<logos::bootstrap_client>> idle;
	std::atomic<unsigned> connections;
	std::atomic<unsigned> pulling;
	std::shared_ptr<logos::node> node;
	std::atomic<unsigned> account_count;
	std::atomic<uint64_t> total_blocks;
	std::vector<std::pair<logos::block_hash, logos::block_hash>> bulk_push_targets;
	bool stopped;
	std::mutex mutex;
	std::condition_variable condition;
};
class frontier_req_client : public std::enable_shared_from_this<logos::frontier_req_client>
{
public:
	frontier_req_client (std::shared_ptr<logos::bootstrap_client>);
	~frontier_req_client ();
	void run ();
	void receive_frontier ();
	void received_frontier (boost::system::error_code const &, size_t);
	void request_account (logos::account const &, logos::block_hash const &);
	void unsynced (MDB_txn *, logos::block_hash const &, logos::block_hash const &);
	void next (MDB_txn *);
	void insert_pull (logos::pull_info const &);
	std::shared_ptr<logos::bootstrap_client> connection;
	logos::account current;
	logos::account_info info;
	unsigned count;
	logos::account landing;
	logos::account faucet;
	std::chrono::steady_clock::time_point start_time;
	std::promise<bool> promise;
	/** A very rough estimate of the cost of `bulk_push`ing missing blocks */
	uint64_t bulk_push_cost;
};
class bulk_pull_client : public std::enable_shared_from_this<logos::bulk_pull_client>
{
public:
	bulk_pull_client (std::shared_ptr<logos::bootstrap_client>, logos::pull_info const &);
	~bulk_pull_client ();
	void request ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t);
	logos::block_hash first ();
	std::shared_ptr<logos::bootstrap_client> connection;
	logos::block_hash expected;
	logos::pull_info pull;
};
class bootstrap_client : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<logos::node>, std::shared_ptr<logos::bootstrap_attempt>, logos::tcp_endpoint const &);
	~bootstrap_client ();
	void run ();
	std::shared_ptr<logos::bootstrap_client> shared ();
	void start_timeout ();
	void stop_timeout ();
	void stop (bool force);
	double block_rate () const;
	double elapsed_seconds () const;
	std::shared_ptr<logos::node> node;
	std::shared_ptr<logos::bootstrap_attempt> attempt;
	boost::asio::ip::tcp::socket socket;
	logos::socket_timeout timeout;
	std::array<uint8_t, 200> receive_buffer;
	logos::tcp_endpoint endpoint;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<uint64_t> block_count;
	std::atomic<bool> pending_stop;
	std::atomic<bool> hard_stop;
};
class bulk_push_client : public std::enable_shared_from_this<logos::bulk_push_client>
{
public:
	bulk_push_client (std::shared_ptr<logos::bootstrap_client> const &);
	~bulk_push_client ();
	void start ();
	void push (MDB_txn *);
	void push_block (logos::block const &);
	void send_finished ();
	std::shared_ptr<logos::bootstrap_client> connection;
	std::promise<bool> promise;
	std::pair<logos::block_hash, logos::block_hash> current_target;
};
class bootstrap_initiator
{
public:
	bootstrap_initiator (logos::node &);
	~bootstrap_initiator ();
	void bootstrap (logos::endpoint const &, bool add_to_peers = true);
	void bootstrap ();
	void run_bootstrap ();
	void notify_listeners (bool);
	void add_observer (std::function<void(bool)> const &);
	bool in_progress ();
	std::shared_ptr<logos::bootstrap_attempt> current_attempt ();
	void process_fork (MDB_txn *, std::shared_ptr<logos::block>);
	void stop ();

private:
	logos::node & node;
	std::shared_ptr<logos::bootstrap_attempt> attempt;
	bool stopped;
	std::mutex mutex;
	std::condition_variable condition;
	std::vector<std::function<void(bool)>> observers;
	std::thread thread;
};
class bootstrap_server;
class bootstrap_listener
{
public:
	bootstrap_listener (boost::asio::io_service &, uint16_t, logos::node &);
	void start ();
	void stop ();
	void accept_connection ();
	void accept_action (boost::system::error_code const &, std::shared_ptr<boost::asio::ip::tcp::socket>);
	std::mutex mutex;
	std::unordered_map<logos::bootstrap_server *, std::weak_ptr<logos::bootstrap_server>> connections;
	logos::tcp_endpoint endpoint ();
	boost::asio::ip::tcp::acceptor acceptor;
	logos::tcp_endpoint local;
	boost::asio::io_service & service;
	logos::node & node;
	bool on;
};
class message;
class bootstrap_server : public std::enable_shared_from_this<logos::bootstrap_server>
{
public:
	bootstrap_server (std::shared_ptr<boost::asio::ip::tcp::socket>, std::shared_ptr<logos::node>);
	~bootstrap_server ();
	void receive ();
	void receive_header_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_blocks_action (boost::system::error_code const &, size_t);
	void receive_frontier_req_action (boost::system::error_code const &, size_t);
	void receive_bulk_push_action ();
	void add_request (std::unique_ptr<logos::message>);
	void finish_request ();
	void run_next ();
	std::array<uint8_t, 128> receive_buffer;
	std::shared_ptr<boost::asio::ip::tcp::socket> socket;
	std::shared_ptr<logos::node> node;
	std::mutex mutex;
	std::queue<std::unique_ptr<logos::message>> requests;
};
class bulk_pull;
class bulk_pull_server : public std::enable_shared_from_this<logos::bulk_pull_server>
{
public:
	bulk_pull_server (std::shared_ptr<logos::bootstrap_server> const &, std::unique_ptr<logos::bulk_pull>);
	void set_current_end ();
	std::unique_ptr<logos::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<logos::bootstrap_server> connection;
	std::unique_ptr<logos::bulk_pull> request;
	std::vector<uint8_t> send_buffer;
	logos::block_hash current;
};
class bulk_pull_blocks;
class bulk_pull_blocks_server : public std::enable_shared_from_this<logos::bulk_pull_blocks_server>
{
public:
	bulk_pull_blocks_server (std::shared_ptr<logos::bootstrap_server> const &, std::unique_ptr<logos::bulk_pull_blocks>);
	void set_params ();
	std::unique_ptr<logos::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<logos::bootstrap_server> connection;
	std::unique_ptr<logos::bulk_pull_blocks> request;
	std::vector<uint8_t> send_buffer;
	logos::store_iterator stream;
	logos::transaction stream_transaction;
	uint32_t sent_count;
	logos::block_hash checksum;
};
class bulk_push_server : public std::enable_shared_from_this<logos::bulk_push_server>
{
public:
	bulk_push_server (std::shared_ptr<logos::bootstrap_server> const &);
	void receive ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t);
	std::array<uint8_t, 256> receive_buffer;
	std::shared_ptr<logos::bootstrap_server> connection;
};
class frontier_req;
class frontier_req_server : public std::enable_shared_from_this<logos::frontier_req_server>
{
public:
	frontier_req_server (std::shared_ptr<logos::bootstrap_server> const &, std::unique_ptr<logos::frontier_req>);
	void skip_old ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
	std::shared_ptr<logos::bootstrap_server> connection;
	logos::account current;
	logos::account_info info;
	std::unique_ptr<logos::frontier_req> request;
	std::vector<uint8_t> send_buffer;
	size_t count;
};
}
