#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <logos/node/node.hpp>
#include <logos/node/utility.hpp>
#include <unordered_map>

namespace logos
{
void error_response_ (std::function<void(boost::property_tree::ptree const &)> response_a, std::string const & message_a);
class node;
/** Configuration options for RPC TLS */
class rpc_secure_config
{
public:
    rpc_secure_config ();
    void serialize_json (boost::property_tree::ptree &) const;
    bool deserialize_json (boost::property_tree::ptree const &);

    /** If true, enable TLS */
    bool enable;
    /** If true, log certificate verification details */
    bool verbose_logging;
    /** Must be set if the private key PEM is password protected */
    std::string server_key_passphrase;
    /** Path to certificate- or chain file. Must be PEM formatted. */
    std::string server_cert_path;
    /** Path to private key file. Must be PEM formatted.*/
    std::string server_key_path;
    /** Path to dhparam file */
    std::string server_dh_path;
    /** Optional path to directory containing client certificates */
    std::string client_certs_path;
};
class rpc_config
{
public:
    rpc_config ();
    rpc_config (bool);
    void serialize_json (boost::property_tree::ptree &) const;
    bool deserialize_json (boost::property_tree::ptree const &);
    boost::asio::ip::address_v6 address;
    uint16_t port;
    bool enable_control;
    uint64_t frontier_request_limit;
    uint64_t chain_request_limit;
    rpc_secure_config secure;
};
enum class payment_status
{
    not_a_status,
    unknown,
    nothing, // Timeout and nothing was received
    //insufficient, // Timeout and not enough was received
    //over, // More than requested received
    //success_fork, // Amount received but it involved a fork
    success // Amount received
};
class wallet;
class payment_observer;
class rpc
{
public:
    rpc (boost::asio::io_service &, logos::node &, logos::rpc_config const &);
    void start ();
    virtual void accept ();
    void stop ();
    void observer_action (logos::account const &);
    boost::asio::ip::tcp::acceptor acceptor;
    std::mutex mutex;
    std::unordered_map<logos::account, std::shared_ptr<logos::payment_observer>> payment_observers;
    logos::rpc_config config;
    logos::node & node;
    bool on;
    static uint16_t const rpc_port = logos::logos_network ==logos::logos_networks::logos_live_network ? 7076 : 55000;
};
class rpc_connection : public std::enable_shared_from_this<logos::rpc_connection>
{
public:
    rpc_connection (logos::node &, logos::rpc &);
    virtual void parse_connection ();
    virtual void read ();
    virtual void write_result (std::string body, unsigned version);
    std::shared_ptr<logos::node> node;
    logos::rpc & rpc;
    boost::asio::ip::tcp::socket socket;
    boost::beast::flat_buffer buffer;
    boost::beast::http::request<boost::beast::http::string_body> request;
    boost::beast::http::response<boost::beast::http::string_body> res;
    std::atomic_flag responded;
};
class payment_observer : public std::enable_shared_from_this<logos::payment_observer>
{
public:
    payment_observer (std::function<void(boost::property_tree::ptree const &)> const &, logos::rpc &, logos::account const &, logos::amount const &);
    ~payment_observer ();
    void start (uint64_t);
    void observe ();
    void timeout ();
    void complete (logos::payment_status);
    std::mutex mutex;
    std::condition_variable condition;
    logos::rpc & rpc;
    logos::account account;
    logos::amount amount;
    std::function<void(boost::property_tree::ptree const &)> response;
    std::atomic_flag completed;
};
class rpc_handler : public std::enable_shared_from_this<logos::rpc_handler>
{
public:
    rpc_handler (logos::node &, logos::rpc &, std::string const &, std::function<void(boost::property_tree::ptree const &)> const &);
    void process_request ();
    void account_balance ();
    void account_block_count ();
    void account_count ();
    void account_create ();
    void account_from_key ();
    void account_history ();
    void account_info ();
    void account_to_key ();
    void account_list ();
    void account_move ();
    void account_remove ();
    void account_representative ();
    void account_representative_set ();
    void account_weight ();
    void accounts_balances ();
    void accounts_create ();
    void accounts_exist ();
    void accounts_frontiers ();
    void accounts_pending ();
    void available_supply ();
    void request_blocks ();
    void request_blocks_latest ();
    void block ();
    //CH void block_confirm ();
    void blocks ();
    void blocks_exist ();
    void block_account ();
    void block_create ();
    void block_hash ();
    void bootstrap ();
    void bootstrap_any ();
    void bootstrap_progress ();
    void chain ();
    //CH void confirmation_history ();
    template <typename  CT>
    void consensus_blocks ();
    void candidates ();
    void delegators ();
    void delegators_count ();
    void deterministic_key ();
    void epochs ();
    void epochs_latest ();
    void frontiers ();
    void history ();
    void key_create ();
    void key_expand ();
    void krai_to_raw ();
    void krai_from_raw ();
    void ledger ();
    void micro_blocks ();
    void micro_blocks_latest ();
    void mrai_to_raw ();
    void mrai_from_raw ();
    void password_change ();
    void password_enter ();
    void password_valid (bool wallet_locked);
    void payment_begin ();
    void payment_init ();
    void payment_end ();
    void payment_wait ();
    void pending ();
    void pending_exists ();
    void process ();
    void process(std::shared_ptr<Request> request);
    void rai_to_raw ();
    void rai_from_raw ();
    void receive ();
    void receive_minimum ();
    void receive_minimum_set ();
    void representatives ();
    void representatives_online ();
    //void republish ();
    void search_pending ();
    void search_pending_all ();
    void send ();
    void stats ();
    void stop ();
    void successors ();
    void tokens_info ();
    void txacceptor_add ();
    void txacceptor_delete ();
    void txacceptor_update (bool add);
    void unchecked ();
    void unchecked_clear ();
    void unchecked_get ();
    void unchecked_keys ();
    void validate_account_number ();
    void version ();
    void wallet_add ();
    void wallet_add_watch ();
    void wallet_balance_total ();
    void wallet_balances ();
    void wallet_change_seed ();
    void wallet_contains ();
    void wallet_create ();
    void wallet_destroy ();
    void wallet_export ();
    void wallet_frontiers ();
    void wallet_key_valid ();
    void wallet_ledger ();
    void wallet_lock ();
    void wallet_pending ();
    void wallet_representative ();
    void wallet_representative_set ();
    //void wallet_republish ();
    void wallet_work_get ();
    void work_generate ();
    void work_cancel ();
    void work_get ();
    void work_set ();
    void work_validate ();
    void work_peer_add ();
    void work_peers ();
    void work_peers_clear ();
    void buffer_complete ();
    bool is_logos_request ();
    bool should_buffer_request ();
    bool flag_present (const std::string & flag_name);

    // Sleeve commands
    void sleeve_unlock ();
    void sleeve_lock ();
    void sleeve_update_password ();
    void sleeve_store_keys ();
    void unsleeve ();
    void sleeve_reset ();
    void delegate_activate (bool activate = true);
    void cancel_activation_scheduling ();
    void activation_status ();
    void new_bls_key_pair ();
    void new_ecies_key_pair ();

    template<typename F, typename G>
    void check_control(F f, bool condition, G get_err_str)
    {
        if (!condition)
        {
            error_response_ (response, get_err_str());
            return;
        }
        f();
    }

    template<typename F>
    void identity_control(F f)
    {
        auto get_err_str = [](){return SleeveResultToString(sleeve_code::identity_control_disabled);};
        check_control(f, node.config.identity_control_enabled, get_err_str);
    }

    template<typename F>
    void rpc_control(F f)
    {
        auto get_err_str = [](){return "RPC control is disabled";};
        check_control(f, rpc.config.enable_control, get_err_str);
    }

    template<typename T>
    struct RpcResponse
    {
        T contents;
        bool error = false;
        std::string error_msg = "";
    };

    using BoostJson = boost::property_tree::ptree;
    using BlockStore = logos::block_store;

    RpcResponse<BoostJson> tokens_info(const BoostJson& request, BlockStore& store);
    RpcResponse<BoostJson> account_info(const BoostJson& request, BlockStore& store);
    RpcResponse<BoostJson> account_balance(const BoostJson& request, BlockStore& store);
    RpcResponse<BoostJson> accounts_exist(const BoostJson& request, BlockStore& store);
    RpcResponse<BoostJson> block(const BoostJson& request, BlockStore& store);
    RpcResponse<BoostJson> blocks(const BoostJson& request, BlockStore& store);
    RpcResponse<BoostJson> blocks_exist(const BoostJson& request, BlockStore& store);


    std::string body;
    logos::node & node;
    logos::rpc & rpc;
    boost::property_tree::ptree request;
    std::function<void(boost::property_tree::ptree const &)> response;
};
/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<logos::rpc> get_rpc (boost::asio::io_service & service_a, logos::node & node_a, logos::rpc_config const & config_a);
}
