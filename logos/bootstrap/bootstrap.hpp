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

#include <logos/bootstrap/batch_block_bulk_pull.hpp>

constexpr double bootstrap_connection_scale_target_blocks = 50000.0;
constexpr double bootstrap_connection_warmup_time_sec = 5.0;
constexpr double bootstrap_minimum_blocks_per_sec = 10.0;
constexpr double bootstrap_minimum_frontier_blocks_per_sec = 1000.0;
constexpr unsigned bootstrap_frontier_retry_limit = 16;
constexpr double bootstrap_minimum_termination_time_sec = 30.0;
constexpr unsigned bootstrap_max_new_connections = 128; // RGD: Increase limit from 10.
constexpr unsigned bulk_push_cost_limit = 200;

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

struct request_info {
    request_info(
        uint64_t  _timestamp_start,
        uint64_t  _timestamp_end,
        uint64_t  _seq_start,
        uint64_t  _seq_end,
        int       _delegate_id,
        BlockHash _e_start,
        BlockHash _e_end,
        BlockHash _m_start,
        BlockHash _m_end,
        BlockHash _b_start,
        BlockHash _b_end)
    :
        timestamp_start(_timestamp_start),
        timestamp_end(_timestamp_end),
        seq_start(_seq_start),
        seq_end(_seq_end),
        delegate_id(_delegate_id),
        e_start(_e_start),
        e_end(_e_end),
        m_start(_m_start),
        m_end(_m_end),
        b_start(_b_start),
        b_end(_b_end)
    {
    }
    uint64_t  timestamp_start;
    uint64_t  timestamp_end;
    uint64_t  seq_start;
    uint64_t  seq_end;
    int       delegate_id;
    BlockHash e_start;
    BlockHash e_end;
    BlockHash m_start;
    BlockHash m_end;
    BlockHash b_start;
    BlockHash b_end;
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

enum pull_type {
    account_pull     = 0,
    batch_block_pull = 1
};

class pull_info
{
public:
	pull_info ();
	pull_info (logos::account const &, logos::block_hash const &, logos::block_hash const &);
    pull_info (uint64_t start, uint64_t end, uint64_t seq_start, uint64_t seq_end, int delegate_id, BlockHash e_start, BlockHash e_end, BlockHash m_start, BlockHash m_end, BlockHash b_start, BlockHash b_end);
	logos::account account;
	logos::block_hash head;
	logos::block_hash end;
	unsigned attempts;
    uint64_t  timestamp_start;
    uint64_t  timestamp_end;
    uint64_t  seq_start;
    uint64_t  seq_end;
    int       delegate_id;
    BlockHash e_start;
    BlockHash e_end;
    BlockHash m_start;
    BlockHash m_end;
    BlockHash b_start;
    BlockHash b_end;
    pull_type type;
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
    bool still_pushing ();
	void process_fork (MDB_txn *, std::shared_ptr<logos::block>);
	unsigned target_connections (size_t pulls_remaining);
	bool should_log ();
	void add_bulk_push_target (logos::block_hash const &, logos::block_hash const &);
    void add_bulk_push_target (logos::request_info);
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
    std::vector<request_info> req;
	bool stopped;
	std::mutex mutex;
	std::condition_variable condition;
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
	std::array<uint8_t, BatchBlock::bulk_pull_response_mesg_len * 8> receive_buffer; // Was 2
	logos::tcp_endpoint endpoint;
	std::chrono::steady_clock::time_point start_time;
	std::atomic<uint64_t> block_count;
	std::atomic<bool> pending_stop;
	std::atomic<bool> hard_stop;
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
	std::array<uint8_t, logos::bulk_pull::SIZE * 8> receive_buffer; // Was 2
	std::shared_ptr<boost::asio::ip::tcp::socket> socket;
	std::shared_ptr<logos::node> node;
	std::mutex mutex;
	std::queue<std::unique_ptr<logos::message>> requests;
};

}
