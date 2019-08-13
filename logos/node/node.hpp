#pragma once

#include <logos/node/stats.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/epoch/recall_handler.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/persistence/block_cache.hpp>
#include <logos/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/p2p/p2p.h>

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

class ConsensusContainer;
class TxAcceptor;
class TxReceiver;

namespace logos
{

class node;

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
    Handle add (std::chrono::steady_clock::time_point const &, std::function<void()> const &);

    void addAfter(std::function<void()> const &handler, unsigned ms)
    {
        add(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms), handler);
    }

    template<typename REP, typename PERIOD>
    Handle add(std::chrono::duration<REP, PERIOD> const & duration, std::function<void()> const & handler)
    {
        return add(std::chrono::steady_clock::now() + duration,
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
    bool p2p_init;
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
    TxAcceptorConfig tx_acceptor_config;
    p2p_config p2p_conf;
    static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
    static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
    static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
};

class Logos_p2p_interface : public p2p_interface {
private:
    logos::node &_node;
public:
    Logos_p2p_interface(logos::node &node)
        : _node(node)
    {}
    virtual bool ReceiveMessageCallback(const void *message, unsigned size);
    friend class logos::node;
};

class node : public std::enable_shared_from_this<logos::node>
{
public:
    node (logos::node_init &, boost::asio::io_service &, uint16_t, boost::filesystem::path const &, logos::alarm &, logos::logging const &/*, logos::work_pool &*/);
    node (logos::node_init &, boost::asio::io_service &, boost::filesystem::path const &, logos::alarm &, logos::node_config const &/*, logos::work_pool &*/);
    ~node ();
    template <typename T>
    void background (T action_a)
    {
        alarm.service.post (action_a);
    }
    bool copy_with_compaction (boost::filesystem::path const &);
    void start ();
    void stop ();
    std::shared_ptr<logos::node> shared ();
    int store_version ();
    void ongoing_bootstrap ();
    void on_demand_bootstrap (logos_global::BootstrapCompleteCB cb);

    process_return OnRequest(std::shared_ptr<Request> request,
                             bool should_buffer);
    process_return BufferComplete();

    /// update tx acceptor configuration, don't allow switch between the delegate and standalone modes
    /// @param ip acceptor's ip
    /// @param port acceptor's port
    /// @param add true if adding
    /// @returns true if can update
    bool update_tx_acceptor(const std::string &ip, uint16_t port, bool add);


    boost::asio::io_service & service;
    logos::node_config config;
    logos::alarm & alarm;
    Log log;
    logos::block_store store;
    logos::BlockCache block_cache;
    boost::filesystem::path application_path;
    logos::stat stats;
    RecallHandler _recall_handler;
    Logos_p2p_interface p2p;
    DelegateIdentityManager _identity_manager;
    Archiver _archiver;
    std::shared_ptr<ConsensusContainer> _consensus_container;
    std::shared_ptr<TxAcceptor> _tx_acceptor;
    std::shared_ptr<TxReceiver> _tx_receiver;
    Bootstrap::BootstrapInitiator bootstrap_initiator;
    Bootstrap::BootstrapListener bootstrap_listener;

    p2p_config p2p_conf;
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
    std::shared_ptr<logos::node> node;
};
}
