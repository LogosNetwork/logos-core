#pragma once

#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull_response.hpp>
#include <logos/bootstrap/microblock.hpp>

namespace logos {

class bulk_pull_client : public std::enable_shared_from_this<logos::bulk_pull_client>
{
public:
    /// Class constructor
    /// @param bootstrap_client
    /// @param pull_info
    bulk_pull_client (std::shared_ptr<logos::bootstrap_client>, logos::pull_info const &);

    /// Class desctructor
    ~bulk_pull_client ();

    /// request_batch_block start of operation
    void request_batch_block(); // NOTE

    /// receive_block composed operation
    void receive_block ();

    /// received_type composed operation, receive 1 byte indicating block type
    void received_type ();

    /// received_block_size composed operation, receive the 4 byte size of the message
    void received_block_size(boost::system::error_code const &, size_t);

    /// received_block composed operation, receive the actual block
    void received_block (boost::system::error_code const &, size_t);

    std::shared_ptr<logos::bootstrap_client> connection;
    logos::block_hash expected;
    logos::block_hash end_transmission;
    logos::pull_info pull;
};

class bulk_pull;
class bulk_pull_server : public std::enable_shared_from_this<logos::bulk_pull_server>
{
public:
    /// Class constructor
    /// @param bootstrap_server
    /// @param bulk_pull (the actual request being made)
    bulk_pull_server (std::shared_ptr<logos::bootstrap_server> const &, std::unique_ptr<logos::bulk_pull>);

    /// set_current_end sets end of transmission
    void set_current_end ();//walk backwards, set start and end

    /// send_next sends next block
    void send_next ();

    /// sent_action composed operation
    void sent_action (boost::system::error_code const &, size_t);

    /// send_finished send end of transmission
    void send_finished ();

    /// no_block_sent
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
