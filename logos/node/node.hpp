#pragma once

#define _PRODUCTION 1

#include <logos/ledger.hpp>
#include <logos/lib/work.hpp>
#include <logos/node/stats.hpp>
#include <logos/node/wallet.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/epoch/recall_handler.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/bootstrap/bootstrap_interface.hpp>
#include <logos/bootstrap/batch_block_validator.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

#include <boost/asio.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/log/trivial.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <miniupnpc.h>

namespace boost
{
namespace program_options
{
    class options_description;
    class variables_map;
}
}

namespace logos
{
class node;

class vote_info
{
public:
    std::chrono::steady_clock::time_point time;
    uint64_t sequence;
    logos::block_hash hash;
    bool operator< (logos::vote const &) const;
};


struct operation
{
    using Handle    = uint64_t;
    using TimePoint = std::chrono::steady_clock::time_point;

    bool operator> (logos::operation const &) const;

    TimePoint             wakeup;
    std::function<void()> function;
    Handle                id;
};
class alarm
{
public:

    using Handle         = uint64_t;
    using OperationQueue = std::priority_queue<operation,
                                               std::vector<operation>,
                                               std::greater<operation>
                                               >;

    alarm (boost::asio::io_service &);
    ~alarm ();
    void add (std::chrono::steady_clock::time_point const &, std::function<void()> const &);

    template<typename REP, typename PERIOD>
    Handle add(std::chrono::duration<REP, PERIOD> const & duration, std::function<void()> const & handler)
    {
        add(std::chrono::steady_clock::now() + duration,
            handler);
    }

    void run ();

    void cancel(Handle handle);

    void remove_operation(Handle handle);

    boost::asio::io_service &  service;
    std::unordered_set<Handle> pending_operations;
    std::mutex                 mutex;
    std::condition_variable    condition;
    OperationQueue             operations;
    std::thread                thread;
    Handle                     operation_handle;
};
class gap_information
{
public:
    std::chrono::steady_clock::time_point arrival;
    logos::block_hash hash;
    std::unique_ptr<logos::votes> votes;
};
class gap_cache
{
public:
    gap_cache (logos::node &);
    void add (MDB_txn *, std::shared_ptr<logos::block>);
    //void vote (std::shared_ptr<logos::vote>);
    //logos::uint128_t bootstrap_threshold (MDB_txn *);
    void purge_old ();
    boost::multi_index_container<
    logos::gap_information,
    boost::multi_index::indexed_by<
    boost::multi_index::ordered_non_unique<boost::multi_index::member<gap_information, std::chrono::steady_clock::time_point, &gap_information::arrival>>,
    boost::multi_index::hashed_unique<boost::multi_index::member<gap_information, logos::block_hash, &gap_information::hash>>>>
    blocks;
    size_t const max = 256;
    std::mutex mutex;
    logos::node & node;
};
class work_pool;
class peer_information
{
public:
    peer_information (logos::endpoint const &, unsigned);
    peer_information (logos::endpoint const &, std::chrono::steady_clock::time_point const &, std::chrono::steady_clock::time_point const &);
    logos::endpoint endpoint;
    std::chrono::steady_clock::time_point last_contact;
    std::chrono::steady_clock::time_point last_attempt;
    std::chrono::steady_clock::time_point last_bootstrap_attempt;
    std::chrono::steady_clock::time_point last_rep_request;
    std::chrono::steady_clock::time_point last_rep_response;
    logos::amount rep_weight;
    logos::account probable_rep_account;
    unsigned network_version;
};
class peer_attempt
{
public:
    logos::endpoint endpoint;
    std::chrono::steady_clock::time_point last_attempt;
};
class peer_container
{
public:
    peer_container (logos::endpoint const &);
    // We were contacted by endpoint, update peers
    void contacted (logos::endpoint const &, unsigned);
    // Unassigned, reserved, self
    bool not_a_peer (logos::endpoint const &);
    // Returns true if peer was already known
    bool known_peer (logos::endpoint const &);
    // Notify of peer we received from
    bool insert (logos::endpoint const &, unsigned);
    std::unordered_set<logos::endpoint> random_set (size_t);
    void random_fill (std::array<logos::endpoint, 8> &);
    // Request a list of the top known representatives
    std::vector<peer_information> representatives (size_t);
    // List of all peers
    std::deque<logos::endpoint> list ();
    std::map<logos::endpoint, unsigned> list_version ();
    // A list of random peers sized for the configured rebroadcast fanout
    std::deque<logos::endpoint> list_fanout ();
    // Get the next peer for attempting bootstrap
    logos::endpoint bootstrap_peer ();
    // Purge any peer where last_contact < time_point and return what was left
    std::vector<logos::peer_information> purge_list (std::chrono::steady_clock::time_point const &);
    //CH std::vector<logos::endpoint> rep_crawl ();
    bool rep_response (logos::endpoint const &, logos::account const &, logos::amount const &);
    void rep_request (logos::endpoint const &);
    // Should we reach out to this endpoint with a keepalive message
    bool reachout (logos::endpoint const &);
    size_t size ();
    size_t size_sqrt ();
    bool empty ();
    std::mutex mutex;
    logos::endpoint self;
    boost::multi_index_container<
    peer_information,
    boost::multi_index::indexed_by<
    boost::multi_index::hashed_unique<boost::multi_index::member<peer_information, logos::endpoint, &peer_information::endpoint>>,
    boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_contact>>,
    boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_attempt>, std::greater<std::chrono::steady_clock::time_point>>,
    boost::multi_index::random_access<>,
    boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_bootstrap_attempt>>,
    boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_rep_request>>,
    boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, logos::amount, &peer_information::rep_weight>, std::greater<logos::amount>>>>
    peers;
    boost::multi_index_container<
    peer_attempt,
    boost::multi_index::indexed_by<
    boost::multi_index::hashed_unique<boost::multi_index::member<peer_attempt, logos::endpoint, &peer_attempt::endpoint>>,
    boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_attempt, std::chrono::steady_clock::time_point, &peer_attempt::last_attempt>>>>
    attempts;
    // Called when a new peer is observed
    std::function<void(logos::endpoint const &)> peer_observer;
    std::function<void()> disconnect_observer;
    // Number of peers to crawl for being a rep every period
    static size_t constexpr peers_per_crawl = 8;
};
class send_info
{
public:
    uint8_t const * data;
    size_t size;
    logos::endpoint endpoint;
    std::function<void(boost::system::error_code const &, size_t)> callback;
};
class mapping_protocol
{
public:
    char const * name;
    int remaining;
    boost::asio::ip::address_v4 external_address;
    uint16_t external_port;
};
// These APIs aren't easy to understand so comments are verbose
class port_mapping
{
public:
    port_mapping (logos::node &);
    void start ();
    void stop ();
    void refresh_devices ();
    // Refresh when the lease ends
    void refresh_mapping ();
    // Refresh occasionally in case router loses mapping
    void check_mapping_loop ();
    int check_mapping ();
    bool has_address ();
    std::mutex mutex;
    logos::node & node;
    UPNPDev * devices; // List of all UPnP devices
    UPNPUrls urls; // Something for UPnP
    IGDdatas data; // Some other UPnP thing
    // Primes so they infrequently happen at the same time
    static int constexpr mapping_timeout = logos::logos_network ==logos::logos_networks::logos_test_network ? 53 : 3593;
    static int constexpr check_timeout = logos::logos_network ==logos::logos_networks::logos_test_network ? 17 : 53;
    boost::asio::ip::address_v4 address;
    std::array<mapping_protocol, 2> protocols;
    uint64_t check_count;
    bool on;
};
class block_arrival_info
{
public:
    std::chrono::steady_clock::time_point arrival;
    logos::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival
{
public:
    // Return `true' to indicated an error if the block has already been inserted
    bool add (logos::block_hash const &);
    bool recent (logos::block_hash const &);
    boost::multi_index_container<
    logos::block_arrival_info,
    boost::multi_index::indexed_by<
    boost::multi_index::ordered_non_unique<boost::multi_index::member<logos::block_arrival_info, std::chrono::steady_clock::time_point, &logos::block_arrival_info::arrival>>,
    boost::multi_index::hashed_unique<boost::multi_index::member<logos::block_arrival_info, logos::block_hash, &logos::block_arrival_info::hash>>>>
    arrival;
    std::mutex mutex;
    static size_t constexpr arrival_size_min = 8 * 1024;
    static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};
class rep_last_heard_info
{
public:
    std::chrono::steady_clock::time_point last_heard;
    logos::account representative;
};

class network
{
public:
    network (logos::node &, uint16_t);
    void receive ();
    void stop ();
    void receive_action (boost::system::error_code const &, size_t);
    void rpc_action (boost::system::error_code const &, size_t);
    //void republish_vote (std::shared_ptr<logos::vote>);
    //void republish_block (MDB_txn *, std::shared_ptr<logos::block>);
    //void republish (logos::block_hash const &, std::shared_ptr<std::vector<uint8_t>>, logos::endpoint);
    //void publish_broadcast (std::vector<logos::peer_information> &, std::unique_ptr<logos::block>);
    //void confirm_send (logos::confirm_ack const &, std::shared_ptr<std::vector<uint8_t>>, logos::endpoint const &);
    void merge_peers (std::array<logos::endpoint, 8> const &);
    void send_keepalive (logos::endpoint const &);
    //void broadcast_confirm_req (std::shared_ptr<logos::block>);
    //void broadcast_confirm_req_base (std::shared_ptr<logos::block>, std::shared_ptr<std::vector<logos::peer_information>>, unsigned);
    //void send_confirm_req (logos::endpoint const &, std::shared_ptr<logos::block>);
    void send_buffer (uint8_t const *, size_t, logos::endpoint const &, std::function<void(boost::system::error_code const &, size_t)>);
    logos::endpoint endpoint ();
    logos::endpoint remote;
    std::array<uint8_t, 512> buffer;
    boost::asio::ip::udp::socket socket;
    std::mutex socket_mutex;
    boost::asio::ip::udp::resolver resolver;
    logos::node & node; 
    bool on;
    static uint16_t const node_port = logos::logos_network ==logos::logos_networks::logos_live_network ? 7075 : 54000;
};
class logging
{
public:
    logging ();
    void serialize_json (boost::property_tree::ptree &) const;
    bool deserialize_json (bool &, boost::property_tree::ptree &);
    bool upgrade_json (unsigned, boost::property_tree::ptree &);
    bool ledger_logging () const;
    bool ledger_duplicate_logging () const;
    bool vote_logging () const;
    bool network_logging () const;
    bool network_message_logging () const;
    bool network_publish_logging () const;
    bool network_packet_logging () const;
    bool network_keepalive_logging () const;
    bool node_lifetime_tracing () const;
    bool insufficient_work_logging () const;
    bool log_rpc () const;
    bool bulk_pull_logging () const;
    bool callback_logging () const;
    bool work_generation_time () const;
    bool log_to_cerr () const;
    void init (boost::filesystem::path const &);

    std::string log_level;
    bool ledger_logging_value;
    bool ledger_duplicate_logging_value;
    bool vote_logging_value;
    bool network_logging_value;
    bool network_message_logging_value;
    bool network_publish_logging_value;
    bool network_packet_logging_value;
    bool network_keepalive_logging_value;
    bool node_lifetime_tracing_value;
    bool insufficient_work_logging_value;
    bool log_rpc_value;
    bool bulk_pull_logging_value;
    bool work_generation_time_value;
    bool log_to_cerr_value;
    bool flush;
    uintmax_t max_size;
    uintmax_t rotation_size;
    boost::log::sources::logger_mt log;
};
class node_init
{
public:
    node_init ();
    bool error ();
    bool block_store_init;
    bool wallet_init;
};
class node_config
{
public:
    node_config ();
    node_config (uint16_t, logos::logging const &);
    void serialize_json (boost::property_tree::ptree &) const;
    bool deserialize_json (bool &, boost::property_tree::ptree &);
    bool upgrade_json (unsigned, boost::property_tree::ptree &);
    logos::account random_representative ();
    uint16_t peering_port;
    logos::logging logging;
    std::vector<std::pair<std::string, uint16_t>> work_peers;
    std::vector<std::string> preconfigured_peers;
    std::vector<logos::account> preconfigured_representatives;
    unsigned bootstrap_fraction_numerator;
    logos::amount receive_minimum;
    logos::amount online_weight_minimum;
    unsigned online_weight_quorum;
    unsigned password_fanout;
    unsigned io_threads;
    unsigned work_threads;
    bool enable_voting;
    unsigned bootstrap_connections;
    unsigned bootstrap_connections_max;
    std::string callback_address;
    uint16_t callback_port;
    std::string callback_target;
    int lmdb_max_dbs;
    logos::stat_config stat_config;
    logos::block_hash state_block_parse_canary;
    logos::block_hash state_block_generate_canary;
    ConsensusManagerConfig consensus_manager_config;
    static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
    static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
    static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
};
class node_observers
{
public:
    logos::observer_set<std::shared_ptr<logos::block>, logos::account const &, logos::uint128_t const &, bool> blocks;
    logos::observer_set<bool> wallet;
    logos::observer_set<std::shared_ptr<logos::vote>, logos::endpoint const &> vote;
    logos::observer_set<logos::account const &, bool> account_balance;
    logos::observer_set<logos::endpoint const &> endpoint;
    logos::observer_set<> disconnect;
    logos::observer_set<> started;
};
// Processing blocks is a potentially long IO operation
// This class isolates block insertion from other operations like servicing network operations
class block_processor
{
public:
    block_processor (logos::node &);
    ~block_processor ();
    void stop ();
    void flush ();
    void add (std::shared_ptr<logos::block>);
    void force (std::shared_ptr<logos::block>);
    bool should_log ();
    bool have_blocks ();
    void process_blocks ();
    logos::process_return process_receive_one (MDB_txn *, std::shared_ptr<logos::block>);

private:
    void queue_unchecked (MDB_txn *, logos::block_hash const &);
    void process_receive_many (std::unique_lock<std::mutex> &);
    bool stopped;
    bool active;
    std::chrono::steady_clock::time_point next_log;
    std::deque<std::shared_ptr<logos::block>> blocks;
    std::deque<std::shared_ptr<logos::block>> forced;
    std::condition_variable condition;
    logos::node & node;
    std::mutex mutex;
};
class node : public std::enable_shared_from_this<logos::node>
{
public:
    node (logos::node_init &, boost::asio::io_service &, uint16_t, boost::filesystem::path const &, logos::alarm &, logos::logging const &, logos::work_pool &);
    node (logos::node_init &, boost::asio::io_service &, boost::filesystem::path const &, logos::alarm &, logos::node_config const &, logos::work_pool &);
    ~node ();
    template <typename T>
    void background (T action_a)
    {
        alarm.service.post (action_a);
    }
    void send_keepalive (logos::endpoint const &);
    bool copy_with_compaction (boost::filesystem::path const &);
    void keepalive (std::string const &, uint16_t);
    void start ();
    void stop ();
    std::shared_ptr<logos::node> shared ();
    int store_version ();
    //CHvoid process_confirmed (std::shared_ptr<logos::block>);
    void process_message (logos::message &, logos::endpoint const &);
    void process_active (std::shared_ptr<logos::block>);
    logos::process_return process (logos::block const &);
    void keepalive_preconfigured (std::vector<std::string> const &);
    logos::block_hash latest (logos::account const &);
    logos::uint128_t balance (logos::account const &);
    std::unique_ptr<logos::block> block (logos::block_hash const &);
    std::pair<logos::uint128_t, logos::uint128_t> balance_pending (logos::account const &);
    logos::uint128_t weight (logos::account const &);
    logos::account representative (logos::account const &);
    void ongoing_keepalive ();
    //CH void ongoing_rep_crawl ();
    void ongoing_bootstrap ();
    void ongoing_store_flush ();
    void backup_wallet ();
    int price (logos::uint128_t const &, int);
    void work_generate_blocking (logos::block &);
    uint64_t work_generate_blocking (logos::uint256_union const &);
    void work_generate (logos::uint256_union const &, std::function<void(uint64_t)>);
    void add_initial_peers ();
    //CHvoid block_confirm (std::shared_ptr<logos::block>);
    logos::uint128_t delta ();

    // consensus-related functionality.
    // TODO: refactor
    process_return OnSendRequest(std::shared_ptr<state_block> block,
                                 bool should_buffer);
    process_return BufferComplete();


    boost::asio::io_service & service;
    logos::node_config config;
    logos::alarm & alarm;
    logos::work_pool & work;
    boost::log::sources::logger_mt log;
    logos::block_store store;
    logos::gap_cache gap_cache;
    logos::ledger ledger;
    //CH logos::active_transactions active;
    logos::network network;
    logos::bootstrap_initiator bootstrap_initiator;
    logos::bootstrap_listener bootstrap;
    logos::peer_container peers;
    boost::filesystem::path application_path;
    logos::node_observers observers;
    logos::wallets wallets;
    logos::port_mapping port_mapping;
    //CH logos::vote_processor vote_processor;
    //CH logos::rep_crawler rep_crawler;
    unsigned warmed_up;
    logos::block_processor block_processor;
    std::thread block_processor_thread;
    logos::block_arrival block_arrival;
    //CH logos::online_reps online_reps;
    logos::stat stats;
    BatchBlock::validator *_validator; 
    RecallHandler _recall_handler;
    DelegateIdentityManager _identity_manager;
    Archiver _archiver;
#ifdef _PRODUCTION
    ConsensusContainer _consensus_container;
#endif
    static double constexpr price_max = 16.0;
    static double constexpr free_cutoff = 1024.0;
    static std::chrono::seconds constexpr period = std::chrono::seconds (60);
    static std::chrono::seconds constexpr cutoff = period * 5;
    static std::chrono::minutes constexpr backup_interval = std::chrono::minutes (5);
};
class thread_runner
{
public:
    thread_runner (boost::asio::io_service &, unsigned);
    ~thread_runner ();
    void join ();
    std::vector<std::thread> threads;
};
void add_node_options (boost::program_options::options_description &);
bool handle_node_options (boost::program_options::variables_map &);
class inactive_node
{
public:
    inactive_node (boost::filesystem::path const & path = logos::working_path ());
    ~inactive_node ();
    boost::filesystem::path path;
    boost::shared_ptr<boost::asio::io_service> service;
    logos::alarm alarm;
    logos::logging logging;
    logos::node_init init;
    logos::work_pool work;
    std::shared_ptr<logos::node> node;
};
}
