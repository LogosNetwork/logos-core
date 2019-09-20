#pragma once

#include <logos/node/stats.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/epoch/recall_handler.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/tips.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/persistence/block_cache.hpp>
#include <logos/tx_acceptor/tx_acceptor_config.hpp>
#include <logos/p2p/p2p.h>
#include <logos/node/websocket.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

#include <boost/asio.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/log/sinks.hpp>
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

typedef boost::log::sinks::text_file_backend log_file_backend;
typedef boost::log::sinks::asynchronous_sink<
        log_file_backend,
        boost::log::sinks::bounded_fifo_queue<               /*< log record queueing strategy >*/
                1000,                                        /*< record queue capacity >*/
                boost::log::sinks::drop_on_overflow          /*< overflow handling policy >*/
        >
> log_file_sink_drop;

//the default QueueingStrategyT = unbounded_fifo_queue
typedef boost::log::sinks::asynchronous_sink<
        log_file_backend
> log_file_sink_all;

constexpr int LOGGER_NICE_VALUE = 19;

class logging
{
public:
    logging ();
    ~logging();
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
    bool drop_if_over_flow;
    bool low_priority_thread;
};

/*
 * async file logger with low thread priority
 */
class file_logger
{
public:
    file_logger();
    ~file_logger();
    void init (boost::filesystem::path const & log_file_path,
               uintmax_t rotation_size,
               uintmax_t max_size,
               bool flush,
               bool drop_if_over_flow,
               bool low_priority_thread);
    void flush_and_stop();

private:
    //only one of file_sink_type_drop and file_sink_type_all will be used.
    boost::shared_ptr<log_file_sink_drop> file_sink_type_drop;
    boost::shared_ptr<log_file_sink_all> file_sink_type_all;
    std::shared_ptr<std::thread> log_writer;
    std::atomic<bool> stopped;
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
    bool enable_websocket;
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
    virtual bool IsMessageImportant(const void *message, unsigned size);
    friend class logos::node;
};

struct BootstrapProgress
{
    uint32_t eb_stored = 0;
    uint32_t mb_stored = 0;
    uint64_t rb_stored = 0;

    uint32_t eb_to_process = 0;
    uint32_t mb_to_process = 0;
    uint64_t rb_to_process = 0;

    uint32_t eb_to_download = 0;
    uint32_t mb_to_download = 0;
    uint64_t rb_to_download = 0;

    bool on_going = false;

    BootstrapProgress() = default;

    BootstrapProgress(const Bootstrap::TipSet & my_store, const Bootstrap::TipSet & my_bootstrap, const Bootstrap::TipSet & other, uint8_t mb_Qed, uint8_t eb_Qed)
    {
        eb_stored = my_store.eb.epoch;
        mb_stored = my_store.mb.sqn;
        rb_stored = my_store.ComputeNumberAllRBs();
        my_store.ComputeNumberBlocksBehind(my_bootstrap, eb_to_process, mb_to_process, rb_to_process);
        my_bootstrap.ComputeNumberBlocksBehind(other, eb_to_download, mb_to_download, rb_to_download);

        //adjust to_process of eb and mb, since we download them 1st and update bootstrap_tips only after the right rbs are processed
        mb_to_process += mb_Qed;
        mb_to_download -= mb_Qed;
        eb_to_process += eb_Qed;
        eb_to_download -= eb_Qed;

        on_going = true;
    }

    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        tree.put("on_going_bootstrap", on_going ? "true" : "false");
        tree.put("eb_stored", std::to_string(eb_stored));
        tree.put("mb_stored", std::to_string(mb_stored));
        tree.put("rb_stored", std::to_string(rb_stored));
        tree.put("eb_to_process", std::to_string(eb_to_process));
        tree.put("mb_to_process", std::to_string(mb_to_process));
        tree.put("rb_to_process", std::to_string(rb_to_process));
        tree.put("eb_to_download", std::to_string(eb_to_download));
        tree.put("mb_to_download", std::to_string(mb_to_download));
        tree.put("rb_to_download", std::to_string(rb_to_download));
    }

    std::string ToJson() const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        std::string s = ostream.str();
        return s;
    }
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
    BootstrapProgress CreateBootstrapProgress();
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
    std::shared_ptr<logos::websocket::listener> websocket_server;

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
