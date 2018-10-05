#pragma once

#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/batch_block_bulk_pull.hpp>
#include <logos/bootstrap/microblock.hpp>

namespace logos {

class bulk_push_client : public std::enable_shared_from_this<logos::bulk_push_client>
{
public:
	bulk_push_client (std::shared_ptr<logos::bootstrap_client> const &);
	~bulk_push_client ();
	void start ();
	void push (MDB_txn *);
	void send_finished ();
	std::shared_ptr<logos::bootstrap_client> connection;
	std::promise<bool> promise;
	std::pair<logos::block_hash, logos::block_hash> current_target;
    void send_next();
    BlockHash current_epoch;
    BlockHash current_micro;
    BlockHash current_bsb; 
    int request_id;
    int iter_count;
    logos::request_info *request;
};

class bulk_push_server : public std::enable_shared_from_this<logos::bulk_push_server>
{
public:
	bulk_push_server (std::shared_ptr<logos::bootstrap_server> const &);
	void receive ();
	void receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t);
	std::array<uint8_t, BatchBlock::bulk_pull_response_mesg_len> receive_buffer;
	std::shared_ptr<logos::bootstrap_server> connection;
};

}
