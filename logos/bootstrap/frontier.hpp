#pragma once

#include <logos/bootstrap/bootstrap.hpp>

namespace logos
{

class frontier_req_client : public std::enable_shared_from_this<logos::frontier_req_client>
{
public:
	frontier_req_client (std::shared_ptr<logos::bootstrap_client>);
	~frontier_req_client ();
	void run ();
	void receive_frontier ();
    void receive_frontier_header ();
    void received_batch_block_frontier (boost::system::error_code const &, size_t);
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
    void send_batch_blocks_frontier();
	std::shared_ptr<logos::bootstrap_server> connection;
	logos::account current;
	logos::account_info info;
    int next_delegate;
    int nr_delegate;
	std::shared_ptr<logos::frontier_req> request;
	std::vector<uint8_t> send_buffer;
	size_t count;
};

}
