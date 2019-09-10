#include <logos/node/node.hpp>

#include <logos/consensus/consensus_container.hpp>
#include <logos/tx_acceptor/tx_acceptor.hpp>
#include <logos/tx_acceptor/tx_receiver.hpp>

#include <logos/lib/interface.h>
#include <logos/node/common.hpp>
#include <logos/node/rpc.hpp>
#include <logos/node/client_callback.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/consensus/messages/receive_block.hpp>

#include <algorithm>
#include <future>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_set>

#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <upnpcommands.h>

#include <ed25519-donna/ed25519.h>

double constexpr logos::node::price_max;
double constexpr logos::node::free_cutoff;
std::chrono::seconds constexpr logos::node::period;
std::chrono::seconds constexpr logos::node::cutoff;
std::chrono::minutes constexpr logos::node::backup_interval;

namespace logos_global
{
    logos::file_logger fileLogger;
}

bool logos::operation::operator> (logos::operation const & other_a) const
{
    return wakeup > other_a.wakeup;
}

logos::alarm::alarm (boost::asio::io_service & service_a) :
service (service_a),
thread ([this]() { run (); }),
operation_handle(0)
{
}

logos::alarm::~alarm ()
{
    add (std::chrono::steady_clock::now (), nullptr);
    thread.join ();
}

void logos::alarm::run ()
{
    std::unique_lock<std::mutex> lock (mutex);
    auto done (false);
    while (!done)
    {
        if (!operations.empty ())
        {
            auto & operation (operations.top ());
            if (operation.function)
            {
                if(pending_operations.find(operation.id) == pending_operations.end())
                {
                    remove_operation(operation.id);
                }
                else if (operation.wakeup <= std::chrono::steady_clock::now ())
                {
                    service.post (operation.function);
                    remove_operation(operation.id);
                }
                else
                {
                    auto wakeup (operation.wakeup);
                    condition.wait_until (lock, wakeup);
                }
            }
            else
            {
                done = true;
            }
        }
        else
        {
            condition.wait (lock);
        }
    }
}

logos::alarm::Handle logos::alarm::add (std::chrono::steady_clock::time_point const & wakeup_a, std::function<void()> const & operation)
{
    std::lock_guard<std::mutex> lock(mutex);

    Handle handle = operation_handle++;

    operations.push(logos::operation({wakeup_a, operation, handle}));
    pending_operations.insert(handle);

    condition.notify_all();

    return handle;
}

void logos::alarm::cancel(Handle handle)
{
    std::lock_guard<std::mutex> lock(mutex);

    pending_operations.erase(handle);
}

void logos::alarm::remove_operation(Handle handle)
{
    operations.pop();
    pending_operations.erase(handle);
}

logos::logging::logging () :
ledger_logging_value (false),
ledger_duplicate_logging_value (false),
vote_logging_value (false),
network_logging_value (true),
network_message_logging_value (false),
network_publish_logging_value (false),
network_packet_logging_value (false),
network_keepalive_logging_value (false),
node_lifetime_tracing_value (false),
insufficient_work_logging_value (true),
log_rpc_value (true),
bulk_pull_logging_value (false),
work_generation_time_value (true),
log_to_cerr_value (false),
max_size (16 * 1024 * 1024),
rotation_size (4 * 1024 * 1024),
flush (false),
drop_if_over_flow (false),
low_priority_thread (false)
{
}

logos::logging::~logging ()
{
    logos_global::fileLogger.flush_and_stop();
}

boost::log::trivial::severity_level get_severity(const std::string& level)
{
    boost::log::trivial::severity_level ret = boost::log::trivial::warning;

    if(level == "trace")
    {
        ret = boost::log::trivial::trace;
    }
    else if(level == "debug")
    {
        ret = boost::log::trivial::debug;
    }
    else if(level == "info")
    {
        ret = boost::log::trivial::info;
    }
    else if(level == "warning")
    {
        ret = boost::log::trivial::warning;
    }
    else if(level == "error")
    {
        ret = boost::log::trivial::error;
    }
    else if(level == "fatal")
    {
        ret = boost::log::trivial::fatal;
    }

    return ret;
}

logos::file_logger::file_logger()
:file_sink_type_drop(nullptr)
,file_sink_type_all(nullptr)
,log_writer(nullptr)
,stopped(false)
{}

logos::file_logger::~file_logger()
{
    flush_and_stop();
}

void logos::file_logger::flush_and_stop()
{
    bool expect = false;
    if (!stopped.compare_exchange_strong(expect, true))
        return;
    if(file_sink_type_drop != nullptr)
    {
        file_sink_type_drop->flush();
        file_sink_type_drop->stop();
        file_sink_type_drop = nullptr;
    }
    if(file_sink_type_all != nullptr)
    {
        file_sink_type_all->flush();
        file_sink_type_all->stop();
        file_sink_type_all = nullptr;
    }
    if(log_writer != nullptr)
    {
        log_writer->join();
        log_writer = nullptr;
    }
}

namespace expressions = boost::log::expressions;

void logos::file_logger::init (boost::filesystem::path const & log_file_path,
                               uintmax_t rotation_size,
                               uintmax_t max_size,
                               bool flush,
                               bool drop_if_over_flow,
                               bool low_priority_thread)
{
    auto file_backend = boost::make_shared<log_file_backend>(
            boost::log::keywords::target = log_file_path / "log",
            boost::log::keywords::file_name = log_file_path / "log" / "log_%Y-%m-%d_%H-%M-%S.%N.log",
            boost::log::keywords::rotation_size = rotation_size,
            boost::log::keywords::auto_flush = flush,
            boost::log::keywords::scan_method = boost::log::sinks::file::scan_method::scan_matching,
            boost::log::keywords::max_size = max_size
    );

    //    std::cerr << "logos::logging::init, drop_if_over_flow="
    //              << drop_if_over_flow
    //              << " low_priority_thread="
    //              << low_priority_thread
    //              << std::endl;

    boost::log::formatter format(expressions::format("[%1% %2% %3%] %4%")
                                 % expressions::max_size_decor< char >(30)[ expressions::stream << std::setw(30) << expressions::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S.%f") ]
                                 % expressions::max_size_decor< char >(18)[ expressions::stream << std::setw(18) << expressions::attr<boost::log::attributes::current_thread_id::value_type>("ThreadID") ]
                                 % expressions::max_size_decor< char >(5)[ expressions::stream << std::setw(5) << boost::log::trivial::severity ]
                                 % expressions::smessage);
    if(drop_if_over_flow)
    {
        file_sink_type_drop = boost::make_shared< log_file_sink_drop >(file_backend, !low_priority_thread);
        file_sink_type_drop->set_formatter(format);
        boost::log::core::get()->add_sink(file_sink_type_drop);
    } else{
        file_sink_type_all = boost::make_shared< log_file_sink_all >(file_backend, !low_priority_thread);
        file_sink_type_all->set_formatter(format);
        boost::log::core::get()->add_sink(file_sink_type_all);
    }

    if(low_priority_thread)
    {
        /*
         * create a log writer thread and try to lower its priority, continue if cannot
        */
        log_writer = std::make_shared<std::thread>([this, drop_if_over_flow]()
        {
#ifdef __linux__
            pthread_setname_np(pthread_self(), "logger");
            /*
             * from the man page http://man7.org/linux/man-pages/man7/sched.7.html
             * For threads scheduled under one of the normal scheduling policies
             * (SCHED_OTHER, SCHED_IDLE, SCHED_BATCH), sched_priority is not used in
             * scheduling decisions (it must be specified as 0).
             */
            struct sched_param param;
            param.sched_priority = 0;
            //most likely, it should already be SCHED_OTHER, just in case...
            int error = sched_setscheduler(0/*this thread*/, SCHED_OTHER, &param);
            if(error)
            {
                std::cerr << "logos::logging::init, sched_setscheduler failed, "
                             "errno=" << error << std::endl;
            }
            else
            {
                int res = nice(LOGGER_NICE_VALUE);
                if(res != LOGGER_NICE_VALUE)
                {
                    std::cerr << "logos::logging::init, nice failed, "
                                 "errno=" << res << std::endl;
                }
            }
#endif
            if(drop_if_over_flow) {
                boost::shared_ptr<log_file_sink_drop> log_writer_sink = file_sink_type_drop;
                log_writer_sink->run();
            } else {
                boost::shared_ptr<log_file_sink_all> log_writer_sink = file_sink_type_all;
                log_writer_sink->run();
            }
        });
        assert(log_writer != nullptr);
    }
}

void logos::logging::init (boost::filesystem::path const & application_path_a)
{
    static std::atomic_flag logging_already_added = ATOMIC_FLAG_INIT;
    if (!logging_already_added.test_and_set ())
    {
        boost::log::add_common_attributes ();
        boost::log::register_simple_formatter_factory< boost::log::trivial::severity_level, char > ("Severity");

        auto level = get_severity(log_level);

        boost::log::core::get()->set_filter (
            boost::log::trivial::severity >= level
            );

        if (log_to_cerr ())
        {
            boost::log::add_console_log (std::cerr, boost::log::keywords::format = "[%TimeStamp% %ThreadID% %Severity%]: %Message%");
        }

        logos_global::fileLogger.init(application_path_a, rotation_size, max_size, flush, drop_if_over_flow, low_priority_thread);
    }
}

void logos::logging::serialize_json (boost::property_tree::ptree & tree_a) const
{
    tree_a.put ("version", "3");
    tree_a.put ("log_level", log_level);
    tree_a.put ("ledger", ledger_logging_value);
    tree_a.put ("ledger_duplicate", ledger_duplicate_logging_value);
    tree_a.put ("vote", vote_logging_value);
    tree_a.put ("network", network_logging_value);
    tree_a.put ("network_message", network_message_logging_value);
    tree_a.put ("network_publish", network_publish_logging_value);
    tree_a.put ("network_packet", network_packet_logging_value);
    tree_a.put ("network_keepalive", network_keepalive_logging_value);
    tree_a.put ("node_lifetime_tracing", node_lifetime_tracing_value);
    tree_a.put ("insufficient_work", insufficient_work_logging_value);
    tree_a.put ("log_rpc", log_rpc_value);
    tree_a.put ("bulk_pull", bulk_pull_logging_value);
    tree_a.put ("work_generation_time", work_generation_time_value);
    tree_a.put ("log_to_cerr", log_to_cerr_value);
    tree_a.put ("max_size", max_size);
    tree_a.put ("rotation_size", rotation_size);
    tree_a.put ("flush", flush);
    tree_a.put ("drop_if_over_flow", drop_if_over_flow);
    tree_a.put ("low_priority_thread", low_priority_thread);
}

bool logos::logging::upgrade_json (unsigned version_a, boost::property_tree::ptree & tree_a)
{
    auto result (false);
    switch (version_a)
    {
        case 1:
            tree_a.put ("vote", vote_logging_value);
            tree_a.put ("version", "2");
            result = true;
        case 2:
            tree_a.put ("rotation_size", "4194304");
            tree_a.put ("flush", "true");
            tree_a.put ("version", "3");
            result = true;
        case 3:
            break;
        default:
            throw std::runtime_error ("Unknown logging_config version");
            break;
    }
    return result;
}

bool logos::logging::deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
{
    auto result (false);
    try
    {
        auto version_l (tree_a.get_optional<std::string> ("version"));
        if (!version_l)
        {
            tree_a.put ("version", "1");
            version_l = "1";
            auto work_peers_l (tree_a.get_child_optional ("work_peers"));
            if (!work_peers_l)
            {
                tree_a.add_child ("work_peers", boost::property_tree::ptree ());
            }
            upgraded_a = true;
        }
        upgraded_a |= upgrade_json (std::stoull (version_l.get ()), tree_a);
        log_level = tree_a.get<std::string> ("log_level", "warning");
        ledger_logging_value = tree_a.get<bool> ("ledger");
        ledger_duplicate_logging_value = tree_a.get<bool> ("ledger_duplicate");
        vote_logging_value = tree_a.get<bool> ("vote");
        network_logging_value = tree_a.get<bool> ("network");
        network_message_logging_value = tree_a.get<bool> ("network_message");
        network_publish_logging_value = tree_a.get<bool> ("network_publish");
        network_packet_logging_value = tree_a.get<bool> ("network_packet");
        network_keepalive_logging_value = tree_a.get<bool> ("network_keepalive");
        node_lifetime_tracing_value = tree_a.get<bool> ("node_lifetime_tracing");
        insufficient_work_logging_value = tree_a.get<bool> ("insufficient_work");
        log_rpc_value = tree_a.get<bool> ("log_rpc");
        bulk_pull_logging_value = tree_a.get<bool> ("bulk_pull");
        work_generation_time_value = tree_a.get<bool> ("work_generation_time");
        log_to_cerr_value = tree_a.get<bool> ("log_to_cerr");
        max_size = tree_a.get<uintmax_t> ("max_size");
        rotation_size = tree_a.get<uintmax_t> ("rotation_size", 4194304);
        flush = tree_a.get<bool> ("flush", true);
        drop_if_over_flow = tree_a.get<bool> ("drop_if_over_flow", false);
        low_priority_thread = tree_a.get<bool> ("low_priority_thread", false);
    }
    catch (std::runtime_error const &)
    {
        result = true;
    }
    return result;
}

bool logos::logging::ledger_logging () const
{
    return ledger_logging_value;
}

bool logos::logging::ledger_duplicate_logging () const
{
    return ledger_logging () && ledger_duplicate_logging_value;
}

bool logos::logging::vote_logging () const
{
    return vote_logging_value;
}

bool logos::logging::network_logging () const
{
    return network_logging_value;
}

bool logos::logging::network_message_logging () const
{
    return network_logging () && network_message_logging_value;
}

bool logos::logging::network_publish_logging () const
{
    return network_logging () && network_publish_logging_value;
}

bool logos::logging::network_packet_logging () const
{
    return network_logging () && network_packet_logging_value;
}

bool logos::logging::network_keepalive_logging () const
{
    return network_logging () && network_keepalive_logging_value;
}

bool logos::logging::node_lifetime_tracing () const
{
    return node_lifetime_tracing_value;
}

bool logos::logging::insufficient_work_logging () const
{
    return network_logging () && insufficient_work_logging_value;
}

bool logos::logging::log_rpc () const
{
    return network_logging () && log_rpc_value;
}

bool logos::logging::bulk_pull_logging () const
{
    return network_logging () && bulk_pull_logging_value;
}

bool logos::logging::callback_logging () const
{
    return network_logging ();
}

bool logos::logging::work_generation_time () const
{
    return work_generation_time_value;
}

bool logos::logging::log_to_cerr () const
{
    return log_to_cerr_value;
}

logos::node_init::node_init () :
block_store_init (false),
wallet_init (false),
p2p_init (false)
{
}

bool logos::node_init::error ()
{
    return block_store_init || wallet_init || p2p_init;
}

logos::node_config::node_config () :
node_config (logos::logos_network == logos::logos_networks::logos_live_network ? 7075 : 54000,
             logos::logging ())
{
}

logos::node_config::node_config (uint16_t peering_port_a, logos::logging const & logging_a) :
peering_port (peering_port_a),
logging (logging_a),
bootstrap_fraction_numerator (1),
receive_minimum (logos::lgs_ratio),
online_weight_minimum (60000 * logos::Glgs_ratio),
online_weight_quorum (50),
password_fanout (1024),
io_threads (std::max<unsigned> (4, std::thread::hardware_concurrency ())),
work_threads (std::max<unsigned> (4, std::thread::hardware_concurrency ())),
enable_voting (true),
enable_websocket (true),
bootstrap_connections (4),
bootstrap_connections_max (64),
callback_port (0),
lmdb_max_dbs (128),
state_block_parse_canary (0),
state_block_generate_canary (0)
{}

void logos::node_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
    tree_a.put ("version", "12");
    tree_a.put ("peering_port", std::to_string (peering_port));
    tree_a.put ("bootstrap_fraction_numerator", std::to_string (bootstrap_fraction_numerator));
    tree_a.put ("receive_minimum", receive_minimum.to_string_dec ());
    boost::property_tree::ptree logging_l;
    logging.serialize_json (logging_l);
    tree_a.add_child ("logging", logging_l);
    boost::property_tree::ptree work_peers_l;
    for (auto i (work_peers.begin ()), n (work_peers.end ()); i != n; ++i)
    {
        boost::property_tree::ptree entry;
        entry.put ("", boost::str (boost::format ("%1%:%2%") % i->first % i->second));
        work_peers_l.push_back (std::make_pair ("", entry));
    }
    tree_a.add_child ("work_peers", work_peers_l);
    boost::property_tree::ptree preconfigured_peers_l;
    for (auto i (preconfigured_peers.begin ()), n (preconfigured_peers.end ()); i != n; ++i)
    {
        boost::property_tree::ptree entry;
        entry.put ("", *i);
        preconfigured_peers_l.push_back (std::make_pair ("", entry));
    }
    tree_a.add_child ("preconfigured_peers", preconfigured_peers_l);
    boost::property_tree::ptree preconfigured_representatives_l;
    for (auto i (preconfigured_representatives.begin ()), n (preconfigured_representatives.end ()); i != n; ++i)
    {
        boost::property_tree::ptree entry;
        entry.put ("", i->to_account ());
        preconfigured_representatives_l.push_back (std::make_pair ("", entry));
    }
    tree_a.add_child ("preconfigured_representatives", preconfigured_representatives_l);
    tree_a.put ("online_weight_minimum", online_weight_minimum.to_string_dec ());
    tree_a.put ("online_weight_quorum", std::to_string (online_weight_quorum));
    tree_a.put ("password_fanout", std::to_string (password_fanout));
    tree_a.put ("io_threads", std::to_string (io_threads));
    tree_a.put ("work_threads", std::to_string (work_threads));
    tree_a.put ("enable_voting", enable_voting);
    tree_a.put ("enable_websocket", enable_websocket);
    tree_a.put ("bootstrap_connections", bootstrap_connections);
    tree_a.put ("bootstrap_connections_max", bootstrap_connections_max);
    tree_a.put ("callback_address", callback_address);
    tree_a.put ("callback_port", std::to_string (callback_port));
    tree_a.put ("callback_target", callback_target);
    tree_a.put ("lmdb_max_dbs", lmdb_max_dbs);
    tree_a.put ("state_block_parse_canary", state_block_parse_canary.to_string ());
    tree_a.put ("state_block_generate_canary", state_block_generate_canary.to_string ());

    tree_a.put ("identity_control_enabled", identity_control_enabled);
    boost::property_tree::ptree consensus_manager;
    consensus_manager_config.SerializeJson(consensus_manager);
    tree_a.add_child("ConsensusManager", consensus_manager);

    boost::property_tree::ptree tx_acceptor;
    tx_acceptor_config.SerializeJson(tx_acceptor);
    tree_a.add_child("TxAcceptor", tx_acceptor);
}

bool logos::node_config::upgrade_json (unsigned version, boost::property_tree::ptree & tree_a)
{
    auto result (false);
    switch (version)
    {
        case 1:
        {
            auto reps_l (tree_a.get_child ("preconfigured_representatives"));
            boost::property_tree::ptree reps;
            for (auto i (reps_l.begin ()), n (reps_l.end ()); i != n; ++i)
            {
                logos::uint256_union account;
                account.decode_account (i->second.get<std::string> (""));
                boost::property_tree::ptree entry;
                entry.put ("", account.to_account ());
                reps.push_back (std::make_pair ("", entry));
            }
            tree_a.erase ("preconfigured_representatives");
            tree_a.add_child ("preconfigured_representatives", reps);
            tree_a.erase ("version");
            tree_a.put ("version", "2");
            result = true;
        }
        case 2:
        {
            tree_a.put ("inactive_supply", logos::uint128_union (0).to_string_dec ());
            tree_a.put ("password_fanout", std::to_string (1024));
            tree_a.put ("io_threads", std::to_string (io_threads));
            tree_a.put ("work_threads", std::to_string (work_threads));
            tree_a.erase ("version");
            tree_a.put ("version", "3");
            result = true;
        }
        case 3:
            tree_a.erase ("receive_minimum");
            tree_a.put ("receive_minimum", logos::lgs_ratio.convert_to<std::string> ());
            tree_a.erase ("version");
            tree_a.put ("version", "4");
            result = true;
        case 4:
            tree_a.erase ("receive_minimum");
            tree_a.put ("receive_minimum", logos::lgs_ratio.convert_to<std::string> ());
            tree_a.erase ("version");
            tree_a.put ("version", "5");
            result = true;
        case 5:
            tree_a.put ("enable_voting", enable_voting);
            tree_a.erase ("packet_delay_microseconds");
            tree_a.erase ("rebroadcast_delay");
            tree_a.erase ("creation_rebroadcast");
            tree_a.erase ("version");
            tree_a.put ("version", "6");
            result = true;
        case 6:
            tree_a.put ("bootstrap_connections", 16);
            tree_a.put ("callback_address", "");
            tree_a.put ("callback_port", "0");
            tree_a.put ("callback_target", "");
            tree_a.erase ("version");
            tree_a.put ("version", "7");
            result = true;
        case 7:
            tree_a.put ("lmdb_max_dbs", "128");
            tree_a.erase ("version");
            tree_a.put ("version", "8");
            result = true;
        case 8:
            tree_a.put ("bootstrap_connections_max", "64");
            tree_a.erase ("version");
            tree_a.put ("version", "9");
            result = true;
        case 9:
            tree_a.put ("state_block_parse_canary", state_block_parse_canary.to_string ());
            tree_a.put ("state_block_generate_canary", state_block_generate_canary.to_string ());
            tree_a.erase ("version");
            tree_a.put ("version", "10");
            result = true;
        case 10:
            tree_a.put ("online_weight_minimum", online_weight_minimum.to_string_dec ());
            tree_a.put ("online_weight_quorom", std::to_string (online_weight_quorum));
            tree_a.erase ("inactive_supply");
            tree_a.erase ("version");
            tree_a.put ("version", "11");
            result = true;
        case 11:
        {
            auto online_weight_quorum_l (tree_a.get<std::string> ("online_weight_quorom"));
            tree_a.erase ("online_weight_quorom");
            tree_a.put ("online_weight_quorum", online_weight_quorum_l);
            tree_a.erase ("version");
            tree_a.put ("version", "12");
            result = true;
        }
        case 12:
            break;
        default:
            throw std::runtime_error ("Unknown node_config version");
    }
    return result;
}

bool logos::node_config::deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
{
    auto result (false);
    try
    {
        auto version_l (tree_a.get_optional<std::string> ("version"));
        if (!version_l)
        {
            tree_a.put ("version", "1");
            version_l = "1";
            auto work_peers_l (tree_a.get_child_optional ("work_peers"));
            if (!work_peers_l)
            {
                tree_a.add_child ("work_peers", boost::property_tree::ptree ());
            }
            upgraded_a = true;
        }
        upgraded_a |= upgrade_json (std::stoull (version_l.get ()), tree_a);
        auto peering_port_l (tree_a.get<std::string> ("peering_port"));
        auto bootstrap_fraction_numerator_l (tree_a.get<std::string> ("bootstrap_fraction_numerator"));
        auto receive_minimum_l (tree_a.get<std::string> ("receive_minimum"));
        auto & logging_l (tree_a.get_child ("logging"));
        work_peers.clear ();
        auto work_peers_l (tree_a.get_child ("work_peers"));
        for (auto i (work_peers_l.begin ()), n (work_peers_l.end ()); i != n; ++i)
        {
            auto work_peer (i->second.get<std::string> (""));
            auto port_position (work_peer.rfind (':'));
            result |= port_position == -1;
            if (!result)
            {
                auto port_str (work_peer.substr (port_position + 1));
                uint16_t port;
                result |= parse_port (port_str, port);
                if (!result)
                {
                    auto address (work_peer.substr (0, port_position));
                    work_peers.push_back (std::make_pair (address, port));
                }
            }
        }
        auto preconfigured_peers_l (tree_a.get_child ("preconfigured_peers"));
        preconfigured_peers.clear ();
        for (auto i (preconfigured_peers_l.begin ()), n (preconfigured_peers_l.end ()); i != n; ++i)
        {
            auto bootstrap_peer (i->second.get<std::string> (""));
            preconfigured_peers.push_back (bootstrap_peer);
        }
        auto preconfigured_representatives_l (tree_a.get_child ("preconfigured_representatives"));
        preconfigured_representatives.clear ();
        for (auto i (preconfigured_representatives_l.begin ()), n (preconfigured_representatives_l.end ()); i != n; ++i)
        {
            logos::account representative (0);
            result = result || representative.decode_account (i->second.get<std::string> (""));
            preconfigured_representatives.push_back (representative);
        }
        if (preconfigured_representatives.empty ())
        {
            result = true;
        }
        auto stat_config_l (tree_a.get_child_optional ("statistics"));
        if (stat_config_l)
        {
            result |= stat_config.deserialize_json (stat_config_l.get ());
        }
        auto online_weight_minimum_l (tree_a.get<std::string> ("online_weight_minimum"));
        auto online_weight_quorum_l (tree_a.get<std::string> ("online_weight_quorum"));
        auto password_fanout_l (tree_a.get<std::string> ("password_fanout"));
        auto io_threads_l (tree_a.get<std::string> ("io_threads"));
        auto work_threads_l (tree_a.get<std::string> ("work_threads"));
        enable_voting = tree_a.get<bool> ("enable_voting");
        enable_websocket = tree_a.get<bool> ("enable_websocket");
        auto bootstrap_connections_l (tree_a.get<std::string> ("bootstrap_connections"));
        auto bootstrap_connections_max_l (tree_a.get<std::string> ("bootstrap_connections_max"));
        callback_address = tree_a.get<std::string> ("callback_address");
        auto callback_port_l (tree_a.get<std::string> ("callback_port"));
        callback_target = tree_a.get<std::string> ("callback_target");
        auto lmdb_max_dbs_l = tree_a.get<std::string> ("lmdb_max_dbs");
        result |= parse_port (callback_port_l, callback_port);
        auto state_block_parse_canary_l = tree_a.get<std::string> ("state_block_parse_canary");
        auto state_block_generate_canary_l = tree_a.get<std::string> ("state_block_generate_canary");
        try
        {
            peering_port = std::stoul (peering_port_l);
            bootstrap_fraction_numerator = std::stoul (bootstrap_fraction_numerator_l);
            password_fanout = std::stoul (password_fanout_l);
            io_threads = std::stoul (io_threads_l);
            work_threads = std::stoul (work_threads_l);
            bootstrap_connections = std::stoul (bootstrap_connections_l);
            bootstrap_connections_max = std::stoul (bootstrap_connections_max_l);
            lmdb_max_dbs = std::stoi (lmdb_max_dbs_l);
            online_weight_quorum = std::stoul (online_weight_quorum_l);
            result |= peering_port > std::numeric_limits<uint16_t>::max ();
            result |= logging.deserialize_json (upgraded_a, logging_l);
            result |= receive_minimum.decode_dec (receive_minimum_l);
            result |= online_weight_minimum.decode_dec (online_weight_minimum_l);
            result |= online_weight_quorum > 100;
            result |= password_fanout < 16;
            result |= password_fanout > 1024 * 1024;
            result |= io_threads == 0;
            result |= state_block_parse_canary.decode_hex (state_block_parse_canary_l);
            result |= state_block_generate_canary.decode_hex (state_block_generate_canary_l);
        }
        catch (std::logic_error const &)
        {
            result = true;
        }

        identity_control_enabled = tree_a.get<bool>("identity_control_enabled", false);
        result |= consensus_manager_config.DeserializeJson(tree_a.get_child("ConsensusManager"));
        try { // temp for backward compatability
            result |= tx_acceptor_config.DeserializeJson(tree_a.get_child("TxAcceptor"));
        }
        catch (...)
        {
            result |= tx_acceptor_config.DeserializeJson(tree_a.get_child("ConsensusManager"));
        }

    }
    catch (std::runtime_error const &)
    {
        result = true;
    }
    return result;
}

logos::account logos::node_config::random_representative ()
{
    assert (preconfigured_representatives.size () > 0);
    size_t index (logos::random_pool.GenerateWord32 (0, preconfigured_representatives.size () - 1));
    auto result (preconfigured_representatives[index]);
    return result;
}

logos::node::node (logos::node_init & init_a, boost::asio::io_service & service_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, logos::alarm & alarm_a, logos::logging const & logging_a/*, logos::work_pool & work_a*/) :
node (init_a, service_a, application_path_a, alarm_a, logos::node_config (peering_port_a, logging_a)/*, work_a*/)
{
}

logos::node::node (logos::node_init & init_a, boost::asio::io_service & service_a, boost::filesystem::path const & application_path_a, logos::alarm & alarm_a, logos::node_config const & config_a/*, logos::work_pool & work_a*/) :
service (service_a),
config (config_a),
alarm (alarm_a),
store (init_a.block_store_init, application_path_a / "data.ldb", config_a.lmdb_max_dbs),
block_cache (service_a,store),
application_path (application_path_a),
stats (config.stat_config),
_recall_handler(),
p2p(*this),
_sleeve(application_path_a / "sleeve.ldb", config_a.password_fanout, init_a.block_store_init),
_identity_manager{std::make_shared<DelegateIdentityManager>(*this, store, alarm.service, _sleeve)},
_consensus_container{std::make_shared<ConsensusContainer>(
        service_a, store, block_cache, alarm_a, config, _recall_handler, *_identity_manager, p2p)},
_tx_acceptor(nullptr),
_tx_receiver(nullptr),
bootstrap_initiator (alarm_a, store, block_cache, _consensus_container->GetPeerInfoProvider()),
bootstrap_listener (alarm_a, store, config.consensus_manager_config.local_address)
{
    BlocksCallback::Instance(service_a, config.callback_address, config.callback_port, config.callback_target, config.logging.callback_logging ());

    LOG_DEBUG (log) << "Node starting, version: " << LOGOS_VERSION_MAJOR << "." << LOGOS_VERSION_MINOR;
    if(config_a.enable_websocket)
    {
        websocket_server = std::make_shared<logos::websocket::listener> (this->service, config.consensus_manager_config.local_address);
        websocket_server->run ();
    }

    p2p_conf = config.p2p_conf;
    p2p_conf.lmdb_env = store.environment.environment;
    p2p_conf.lmdb_dbi = store.p2p_db;
    p2p_conf.boost_io_service = &service;
    init_a.p2p_init = !p2p.Init(p2p_conf);

    if (!init_a.error ())
    {
        if (config.logging.node_lifetime_tracing ())
        {
            LOG_DEBUG (log) << "Constructing node";
        }
    }
}

logos::node::~node ()
{
    if (config.logging.node_lifetime_tracing ())
    {
        BOOST_LOG (log) << "Destructing node";
    }
    stop ();
}

bool logos::node::copy_with_compaction (boost::filesystem::path const & destination_file)
{
    return !mdb_env_copy2 (store.environment.environment,
    destination_file.string ().c_str (), MDB_CP_COMPACT);
}

const logos::node_config & logos::node::GetConfig()
{
    return config;
}

std::shared_ptr<NewEpochEventHandler> logos::node::GetEpochEventHandler()
{
    return _consensus_container;
}

IRecallHandler & logos::node::GetRecallHandler()
{
    return _recall_handler;
}

bool logos::node::P2pPropagateMessage(const void *message, unsigned size, bool output)
{
    return p2p.PropagateMessage(message, size, output);
}

bool logos::node::UpdateTxAcceptor(const std::string &ip, uint16_t port, bool add)
{
    // can't transition from the delegate mode to standalone mode
    // or deleting tx acceptor while in the delegate mode
    if (_tx_acceptor != nullptr)
    {
        return false;
    }

    assert(_tx_receiver != nullptr);

    if (add)
    {
        return _tx_receiver->AddChannel(ip, port);
    }
    else
    {
        return _tx_receiver->DeleteChannel(ip, port);
    }
}

bool logos::parse_port (std::string const & string_a, uint16_t & port_a)
{
    bool result;
    size_t converted;
    try
    {
        port_a = std::stoul (string_a, &converted);
        result = converted != string_a.size () || converted > std::numeric_limits<uint16_t>::max ();
    }
    catch (...)
    {
        result = true;
    }
    return result;
}

void logos::node::start ()
{
    // CH added starting logic here instead of inside constructors

    // TODO: check for bootstrap completion first

    _consensus_container->Start();

    bootstrap_listener.start ();
    ongoing_bootstrap ();
    auto this_l (shared_from_this());
    logos_global::AssignNode(this_l);
}

void logos::node::ActivateConsensus()
{
    _consensus_container->ActivateConsensus();

    if (config.tx_acceptor_config.tx_acceptors.size() == 0)
    {
        _tx_acceptor = std::make_shared<TxAcceptorDelegate>(service, _consensus_container, config);
    }
    if (config.tx_acceptor_config.tx_acceptors.size() != 0)
    {
        _tx_receiver = std::make_shared<TxReceiver>(service, alarm, _consensus_container, config);
    }
    if (_tx_acceptor)
    {
        _tx_acceptor->Start();
    }
    if (_tx_receiver)
    {
        _tx_receiver->Start();
    }
}

void logos::node::DeactivateConsensus()
{
    _consensus_container->DeactivateConsensus();

    // TODO: gracefully stop TxAcceptor and TxReceiver
    _tx_acceptor = nullptr;
    _tx_receiver = nullptr;
}

void logos::node::stop ()
{
    LOG_DEBUG (log) << "Node stopping";
    {
        std::shared_ptr<logos::node> this_l(nullptr);
        logos_global::AssignNode(this_l);
    }

    bootstrap_initiator.stop ();
    bootstrap_listener.stop ();
    p2p.Shutdown ();
    _identity_manager->CancelAdvert();
    if (websocket_server)
    {
        websocket_server->stop ();
    }
}

bool logos::Logos_p2p_interface::ReceiveMessageCallback(const void *message, unsigned size) {
    return _node._consensus_container->OnP2pReceive(message, size);
}

bool logos::Logos_p2p_interface::IsMessageImportant(const void *message, unsigned size) {
    P2pHeader head(0, P2pAppType::Consensus);
    bool error = false;

    logos::bufferstream stream((const unsigned char *)message, size);
    head.Deserialize(error, stream);
    if (error)
    {
        return false;
    }

    /* return true for advertisement messages */
    switch (head.app_type)
    {
    case P2pAppType::Consensus:
        return false;
    case P2pAppType::AddressAd:
        return true;
    case P2pAppType::AddressAdTxAcceptor:
        return true;
    case P2pAppType::Request:
        return false;
    }

    return false;
}

void logos::node::ongoing_bootstrap ()
{
    LOG_TRACE(log) << "node::ongoing_bootstrap";
    auto cb = [this](logos_global::BootstrapResult res){
        LOG_DEBUG(log) << "node::ongoing_bootstrap, callback res="
                       << logos_global::BootstrapResultToString(res);
    };

    auto next_wakeup (60);
    if (! bootstrap_initiator.check_progress ())
    {
        bootstrap_initiator.bootstrap(cb);
    }

    std::weak_ptr<logos::node> node_w (shared_from_this ());
    alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (next_wakeup), [node_w]() {
        if (auto node_l = node_w.lock ())
        {
            node_l->ongoing_bootstrap ();
        }
    });
}

void logos::node::on_demand_bootstrap (logos_global::BootstrapCompleteCB cb)
{
    LOG_DEBUG (log) << __func__;
    bootstrap_initiator.bootstrap (cb);
}

logos::BootstrapProgress logos::node::CreateBootstrapProgress()
{
    LOG_DEBUG (log) << __func__;
    /*
     * A BootstrapProgress object describes
     * (1) how many blocks are stored in the DB,
     * (2) how many blocks have been downloaded and currently precessing
     * (3) how many blocks yet to be downloaded
     *
     * To compute those numbers, we need 3 sets of tips, namely stored_tips, my_tips of the current bootstrap session,
     * and others_tips of the current bootstrap session if there is a session.
     * In addition, because of the way logical bootstrap works, we also need to adjust the number of MB and EB queued
     * in the BlockCache.
     */
    Bootstrap::TipSet my_bootstrap, others;
    uint8_t mb_Qed, eb_Qed;
    bool on_going = bootstrap_initiator.GetTipsets (my_bootstrap, others, mb_Qed, eb_Qed);
    if(on_going)
    {
        auto my_store = Bootstrap::TipSet::CreateTipSet(store);
        return BootstrapProgress(my_store, my_bootstrap, others, mb_Qed, eb_Qed);
    }
    else
    {
        return BootstrapProgress();
    }
}

void Serialize(std::shared_ptr<Request> request,std::vector<uint8_t>& p2p_buffer)
{
    std::vector<uint8_t> buf;
    {
        logos::vectorstream stream(buf);
        request->ToStream(stream,false);
    }

    P2pHeader p2pheader={logos_version, P2pAppType::Request};
    auto hdrs_size = P2pHeader::SIZE;

    std::vector<uint8_t> buf2;
    {
        logos::vectorstream stream(buf2);
        assert(p2pheader.Serialize(stream) == P2pHeader::SIZE);
    }
    assert(hdrs_size == buf2.size());
    p2p_buffer.resize(buf.size() + buf2.size());
    memcpy(p2p_buffer.data(), buf2.data(), buf2.size());
    memcpy(p2p_buffer.data() + hdrs_size, buf.data(), buf.size());
}

logos::process_return logos::node::OnRequest(std::shared_ptr<Request> request, bool should_buffer)
{
    logos::process_return result = _consensus_container->OnDelegateMessage(
            static_pointer_cast<DelegateMessage<ConsensusType::Request>>(request),
            should_buffer);

    LOG_DEBUG(log) << "node::OnRequest - "
        << "hash=" << request->Hash().to_string()
        << ",result=" << ProcessResultToString(result.code);

    if(result.code == logos::process_result::not_delegate)
    {
        if(block_cache.ValidateRequest(request,ConsensusContainer::GetCurEpochNumber(),result))
        {
            std::vector<uint8_t> p2p_buffer;
            Serialize(request,p2p_buffer);

            LOG_DEBUG(log) << "P2PRequestPropagation-hash="
                << request->Hash().to_string()
                << ",submitted,propagating";

            bool propped = p2p.PropagateMessage(p2p_buffer.data(),p2p_buffer.size(),true);

            result.code = propped ? 
                logos::process_result::propagate : 
                logos::process_result::no_propagate;
        }
        else
        {
            LOG_DEBUG(log) << "P2PRequestPropagation-hash="
                << request->Hash().to_string()
                << ",submitted,but invalid,not propagating"
                << ",result=" << ProcessResultToString(result.code);
        }
    }
    return result;


}

logos::process_return logos::node::BufferComplete()
{
    process_return result;

    _consensus_container->BufferComplete(result);

    return result;
}

std::shared_ptr<logos::node> logos::node::shared ()
{
    return shared_from_this ();
}

int logos::node::store_version ()
{
    logos::transaction transaction (store.environment, nullptr, false);
    return store.version_get (transaction);
}

logos::thread_runner::thread_runner (boost::asio::io_service & service_a, unsigned service_threads_a)
{
    for (auto i (0); i < service_threads_a; ++i)
    {
        threads.push_back (std::thread ([&service_a]() {
            try
            {
                service_a.run ();
            }
            catch (const std::exception &exc)
            {
                Log log;
                LOG_FATAL(log) << exc.what();
                trace_and_halt();
            }
            catch (...)
            {
                std::exception_ptr p = std::current_exception();
                Log log;
                LOG_FATAL(log) << "Unhandled service exception! " << (p ? p.__cxa_exception_type()->name() : "null");
                trace_and_halt();
            }
        }));
    }
}

logos::thread_runner::~thread_runner ()
{
    join ();
}

void logos::thread_runner::join ()
{
    for (auto & i : threads)
    {
        if (i.joinable ())
        {
            i.join ();
        }
    }
}

void logos::add_node_options (boost::program_options::options_description & description_a)
{
    // clang-format off
    description_a.add_options ()
        ("account_create", "Insert next deterministic key in to <wallet>")
        ("account_get", "Get account number for the <key>")
        ("account_key", "Get the public key for <account>")
        ("vacuum", "Compact database. If data_path is missing, the database in data directory is compacted.")
        ("snapshot", "Compact database and create snapshot, functions similar to vacuum but does not replace the existing database")
        ("data_path", boost::program_options::value<std::string> (), "Use the supplied path as the data directory")
        ("diagnostics", "Run internal diagnostics")
        ("key_create", "Generates a adhoc random keypair and prints it to stdout")
        ("key_expand", "Derive public key and account number from <key>")
        ("wallet_add_adhoc", "Insert <key> in to <wallet>")
        ("wallet_create", "Creates a new wallet and prints the ID")
        ("wallet_change_seed", "Changes seed for <wallet> to <key>")
        ("wallet_decrypt_unsafe", "Decrypts <wallet> using <password>, !!THIS WILL PRINT YOUR PRIVATE KEY TO STDOUT!!")
        ("wallet_destroy", "Destroys <wallet> and all keys it contains")
        ("wallet_import", "Imports keys in <file> using <password> in to <wallet>")
        ("wallet_list", "Dumps wallet IDs and public keys")
        ("wallet_remove", "Remove <account> from <wallet>")
        ("wallet_representative_get", "Prints default representative for <wallet>")
        ("wallet_representative_set", "Set <account> as default representative for <wallet>")
        ("vote_dump", "Dump most recent votes from representatives")
        ("account", boost::program_options::value<std::string> (), "Defines <account> for other commands")
        ("file", boost::program_options::value<std::string> (), "Defines <file> for other commands")
        ("key", boost::program_options::value<std::string> (), "Defines the <key> for other commands, hex")
        ("password", boost::program_options::value<std::string> (), "Defines <password> for other commands")
        ("wallet", boost::program_options::value<std::string> (), "Defines <wallet> for other commands");
    // clang-format on
}

bool logos::handle_node_options (boost::program_options::variables_map & vm)
{
    auto result (false);
    boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : logos::working_path ();
    if (vm.count ("account_get") > 0)
    {
        if (vm.count ("key") == 1)
        {
            logos::uint256_union pub;
            pub.decode_hex (vm["key"].as<std::string> ());
            std::cout << "Account: " << pub.to_account () << std::endl;
        }
        else
        {
            std::cerr << "account comand requires one <key> option\n";
            result = true;
        }
    }
    else if (vm.count ("account_key") > 0)
    {
        if (vm.count ("account") == 1)
        {
            logos::uint256_union account;
            account.decode_account (vm["account"].as<std::string> ());
            std::cout << "Hex: " << account.to_string () << std::endl;
        }
        else
        {
            std::cerr << "account_key command requires one <account> option\n";
            result = true;
        }
    }
    else if (vm.count ("vacuum") > 0)
    {
        try
        {
            auto vacuum_path = data_path / "vacuumed.ldb";
            auto source_path = data_path / "data.ldb";
            auto backup_path = data_path / "backup.vacuum.ldb";

            std::cout << "Vacuuming database copy in " << data_path << std::endl;
            std::cout << "This may take a while..." << std::endl;

            // Scope the node so the mdb environment gets cleaned up properly before
            // the original file is replaced with the vacuumed file.
            bool success = false;
            {
                inactive_node node (data_path);
                success = node.node->copy_with_compaction (vacuum_path);
            }

            if (success)
            {
                // Note that these throw on failure
                std::cout << "Finalizing" << std::endl;
                boost::filesystem::remove (backup_path);
                boost::filesystem::rename (source_path, backup_path);
                boost::filesystem::rename (vacuum_path, source_path);
                std::cout << "Vacuum completed" << std::endl;
            }
        }
        catch (const boost::filesystem::filesystem_error & ex)
        {
            std::cerr << "Vacuum failed during a file operation: " << ex.what () << std::endl;
        }
        catch (...)
        {
            std::cerr << "Vacuum failed" << std::endl;
        }
    }
    else if (vm.count ("snapshot"))
    {
        try
        {
            boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : logos::working_path ();

            auto source_path = data_path / "data.ldb";
            auto snapshot_path = data_path / "snapshot.ldb";

            std::cout << "Database snapshot of " << source_path << " to " << snapshot_path << " in progress" << std::endl;
            std::cout << "This may take a while..." << std::endl;

            bool success = false;
            {
                inactive_node node (data_path);
                success = node.node->copy_with_compaction (snapshot_path);
            }
            if (success)
            {
                std::cout << "Snapshot completed, This can be found at " << snapshot_path << std::endl;
            }
        }
        catch (const boost::filesystem::filesystem_error & ex)
        {
            std::cerr << "Snapshot failed during a file operation: " << ex.what () << std::endl;
        }
        catch (...)
        {
            std::cerr << "Snapshot Failed" << std::endl;
        }
    }
    else if (vm.count ("key_create"))
    {
        logos::keypair pair;
        std::cout << "Private: " << pair.prv.data.to_string () << std::endl
                  << "Public: " << pair.pub.to_string () << std::endl
                  << "Account: " << pair.pub.to_account () << std::endl;
    }
    else if (vm.count ("key_expand"))
    {
        if (vm.count ("key") == 1)
        {
            logos::uint256_union prv;
            prv.decode_hex (vm["key"].as<std::string> ());
            logos::uint256_union pub;
            ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
            std::cout << "Private: " << prv.to_string () << std::endl
                      << "Public: " << pub.to_string () << std::endl
                      << "Account: " << pub.to_account () << std::endl;
        }
        else
        {
            std::cerr << "key_expand command requires one <key> option\n";
            result = true;
        }
    }
    else
    {
        result = true;
    }
    return result;
}

logos::inactive_node::inactive_node (boost::filesystem::path const & path) :
path (path),
service (boost::make_shared<boost::asio::io_service> ()),
alarm (*service)
{
    boost::filesystem::create_directories (path);
    logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
    logging.init (path);
    node = std::make_shared<logos::node> (init, *service, 24000, path, alarm, logging);
}

logos::inactive_node::~inactive_node ()
{
    node->stop ();
}

void logos_global::FlushAndStopFileLogger()
{
    fileLogger.flush_and_stop();
}
