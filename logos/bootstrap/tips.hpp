#pragma once

#include <logos/bootstrap/bootstrap.hpp>

namespace logos
{

class tips_req_client : public std::enable_shared_from_this<logos::tips_req_client>
{
public:

    /// Class constructor
    /// @param bootstrap_client
    tips_req_client (std::shared_ptr<logos::bootstrap_client>);

    /// Class destructor
    ~tips_req_client ();

    /// run starts the client
    void run ();

    /// receive_tips_header start of receiving the tips from server
    void receive_tips_header ();

    /// received_batch_block_tips final call to receiving tips in composed operation
    /// @param error_code error if there was a problem in the network
    /// @param size_t length of message received
    void received_batch_block_tips (boost::system::error_code const &, size_t);

    /// finish_request
    /// set promise and pool connection
    void finish_request();

    std::shared_ptr<logos::bootstrap_client> connection;

    std::chrono::steady_clock::time_point start_time;
    std::promise<bool> promise;
};

class frontier_req;
class tips_req_server : public std::enable_shared_from_this<logos::tips_req_server>
{
public:

    /// Class constructor
    /// @param bootstrap_server
    /// @param frontier_req (request made by the client)
    tips_req_server (std::shared_ptr<logos::bootstrap_server> const &, std::unique_ptr<logos::frontier_req>);

    /// Class destructor
    ~tips_req_server();

    /// sends the batch block tips to the client
    void send_batch_blocks_tips();

    std::shared_ptr<logos::bootstrap_server> connection;
    std::shared_ptr<logos::frontier_req> request;
    std::vector<uint8_t> send_buffer;
    size_t count;
};

}
