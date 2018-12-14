#pragma once

#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull_response.hpp>
#include <logos/bootstrap/microblock.hpp>

namespace logos {

class bulk_pull_client : public std::enable_shared_from_this<logos::bulk_pull_client>
{
public:
    bulk_pull_client (std::shared_ptr<logos::bootstrap_client>, logos::pull_info const &);
    ~bulk_pull_client ();
    void request_batch_block(); // NOTE
    void receive_block ();
    void received_type ();
    void received_block (boost::system::error_code const &, size_t);
    logos::block_hash first ();
    std::shared_ptr<logos::bootstrap_client> connection;
    logos::block_hash expected;
    logos::block_hash end_transmission;
    logos::pull_info pull;
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
    BlockHash current_epoch;
    BlockHash current_micro;
    BlockHash current_bsb;
    int iter_count;
};

}
