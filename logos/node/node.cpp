#include <logos/node/node.hpp>

#include <logos/consensus/consensus_container.hpp>

#include <logos/lib/interface.h>
#include <logos/node/common.hpp>
#include <logos/node/rpc.hpp>
#include <logos/node/client_callback.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/microblock/microblock.hpp>

#include <logos/lib/trace.hpp>

#include <algorithm>
#include <future>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_set>

#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <upnpcommands.h>

#include <ed25519-donna/ed25519.h>

#define _PRODUCTION 1

double constexpr logos::node::price_max;
double constexpr logos::node::free_cutoff;
std::chrono::seconds constexpr logos::node::period;
std::chrono::seconds constexpr logos::node::cutoff;
std::chrono::minutes constexpr logos::node::backup_interval;
int constexpr logos::port_mapping::mapping_timeout;
int constexpr logos::port_mapping::check_timeout;
size_t constexpr logos::block_arrival::arrival_size_min;
std::chrono::seconds constexpr logos::block_arrival::arrival_time_min;

logos::network::network (logos::node & node_a, uint16_t port) :
socket (node_a.service, logos::endpoint (boost::asio::ip::address_v6::any (), port)),
resolver (node_a.service),
node (node_a),
on (true)
{
}

void logos::network::receive ()
{
    if (node.config.logging.network_packet_logging ())
    {
        BOOST_LOG (node.log) << "Receiving packet";
    }
    std::unique_lock<std::mutex> lock (socket_mutex);
    socket.async_receive_from (boost::asio::buffer (buffer.data (), buffer.size ()), remote, [this](boost::system::error_code const & error, size_t size_a) {
        receive_action (error, size_a);
    });
}

void logos::network::stop ()
{
    on = false;
    socket.close ();
    resolver.cancel ();
}

void logos::network::send_keepalive (logos::endpoint const & endpoint_a)
{
    assert (endpoint_a.address ().is_v6 ());
    logos::keepalive message;
    node.peers.random_fill (message.peers);
    std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
    {
        logos::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (node.config.logging.network_keepalive_logging ())
    {
        BOOST_LOG (node.log) << boost::str (boost::format ("Keepalive req sent to %1%") % endpoint_a);
    }
    std::weak_ptr<logos::node> node_w (node.shared ());
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, node_w, endpoint_a](boost::system::error_code const & ec, size_t) {
        if (auto node_l = node_w.lock ())
        {
            if (ec && node_l->config.logging.network_keepalive_logging ())
            {
                BOOST_LOG (node_l->log) << boost::str (boost::format ("Error sending keepalive to %1%: %2%") % endpoint_a % ec.message ());
            }
            else
            {
                node_l->stats.inc (logos::stat::type::message, logos::stat::detail::keepalive, logos::stat::dir::out);
            }
        }
    });
}

void logos::node::keepalive (std::string const & address_a, uint16_t port_a)
{
    auto node_l (shared_from_this ());
    network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
        if (!ec)
        {
            for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
            {
                auto endpoint (i->endpoint ());
                if (endpoint.address ().is_v4 ())
                {
                    endpoint = logos::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint.address ().to_v4 ()), endpoint.port ());
                }
                node_l->send_keepalive (endpoint);
            }
        }
        else
        {
            BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving address: %1%:%2%: %3%") % address_a % port_a % ec.message ());
        }
    });
}


namespace
{
class network_message_visitor : public logos::message_visitor
{
public:
    network_message_visitor (logos::node & node_a, logos::endpoint const & sender_a) :
    node (node_a),
    sender (sender_a)
    {
    }
    virtual ~network_message_visitor () = default;
    void keepalive (logos::keepalive const & message_a) override
    {
        if (node.config.logging.network_keepalive_logging ())
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("Received keepalive message from %1%") % sender);
        }
        node.stats.inc (logos::stat::type::message, logos::stat::detail::keepalive, logos::stat::dir::in);
        node.peers.contacted (sender, message_a.version_using);
        node.network.merge_peers (message_a.peers);
    }
    void bulk_pull (logos::bulk_pull const &) override
    {
        assert (false);
    }
    void bulk_pull_blocks (logos::bulk_pull_blocks const &) override
    {
        assert (false);
    }
    void bulk_push (logos::bulk_push const &) override
    {
        assert (false);
    }
    void frontier_req (logos::frontier_req const &) override
    {
        assert (false);
    }
    logos::node & node;
    logos::endpoint sender;
};
}

void logos::network::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (!logos::reserved_address (remote) && remote != endpoint ())
        {
            network_message_visitor visitor (node, remote);
            logos::message_parser parser (visitor, node.work);
            parser.deserialize_buffer (buffer.data (), size_a);
            if (parser.status != logos::message_parser::parse_status::success)
            {
                node.stats.inc (logos::stat::type::error);

                if (parser.status == logos::message_parser::parse_status::insufficient_work)
                {
                    if (node.config.logging.insufficient_work_logging ())
                    {
                        BOOST_LOG (node.log) << "Insufficient work in message";
                    }

                    // We've already increment error count, update detail only
                    node.stats.inc_detail_only (logos::stat::type::error, logos::stat::detail::insufficient_work);
                }
                else if (parser.status == logos::message_parser::parse_status::invalid_message_type)
                {
                    if (node.config.logging.network_logging ())
                    {
                        BOOST_LOG (node.log) << "Invalid message type in message";
                    }
                }
                else if (parser.status == logos::message_parser::parse_status::invalid_header)
                {
                    if (node.config.logging.network_logging ())
                    {
                        BOOST_LOG (node.log) << "Invalid header in message";
                    }
                }
                else if (parser.status == logos::message_parser::parse_status::invalid_keepalive_message)
                {
                    if (node.config.logging.network_logging ())
                    {
                        BOOST_LOG (node.log) << "Invalid keepalive message";
                    }
                }
                else if (parser.status == logos::message_parser::parse_status::invalid_publish_message)
                {
                    if (node.config.logging.network_logging ())
                    {
                        BOOST_LOG (node.log) << "Invalid publish message";
                    }
                }
                else if (parser.status == logos::message_parser::parse_status::invalid_confirm_req_message)
                {
                    if (node.config.logging.network_logging ())
                    {
                        BOOST_LOG (node.log) << "Invalid confirm_req message";
                    }
                }
                else if (parser.status == logos::message_parser::parse_status::invalid_confirm_ack_message)
                {
                    if (node.config.logging.network_logging ())
                    {
                        BOOST_LOG (node.log) << "Invalid confirm_ack message";
                    }
                }
                else
                {
                    BOOST_LOG (node.log) << "Could not deserialize buffer";
                }
            }
            else
            {
                node.stats.add (logos::stat::type::traffic, logos::stat::dir::in, size_a);
            }
        }
        else
        {
            if (node.config.logging.network_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Reserved sender %1%") % remote.address ().to_string ());
            }

            node.stats.inc_detail_only (logos::stat::type::error, logos::stat::detail::bad_sender);
        }
        receive ();
    }
    else
    {
        if (error)
        {
            if (node.config.logging.network_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("UDP Receive error: %1%") % error.message ());
            }
        }
        if (on)
        {
            node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [this]() { receive (); });
        }
    }
}

// Send keepalives to all the peers we've been notified of
void logos::network::merge_peers (std::array<logos::endpoint, 8> const & peers_a)
{
    for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
    {
        if (!node.peers.reachout (*i))
        {
            send_keepalive (*i);
        }
    }
}

bool logos::operation::operator> (logos::operation const & other_a) const
{
    return wakeup > other_a.wakeup;
}

logos::alarm::alarm (boost::asio::io_service & service_a) :
service (service_a),
thread ([this]() { run (); })
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

void logos::alarm::add (std::chrono::steady_clock::time_point const & wakeup_a, std::function<void()> const & operation)
{
    std::lock_guard<std::mutex> lock(mutex);

    Handle handle = operation_handle++;

    operations.push(logos::operation({wakeup_a, operation, handle}));
    pending_operations.insert(handle);

    condition.notify_all();
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
flush (true)
{
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
            boost::log::add_console_log (std::cerr, boost::log::keywords::format = "[%TimeStamp% %Severity%]: %Message%");
        }

        boost::log::add_file_log (boost::log::keywords::target = application_path_a / "log",
                                  boost::log::keywords::file_name = application_path_a / "log" / "log_%Y-%m-%d_%H-%M-%S.%N.log",
                                  boost::log::keywords::rotation_size = rotation_size,
                                  boost::log::keywords::auto_flush = flush,
                                  boost::log::keywords::scan_method = boost::log::sinks::file::scan_method::scan_matching,
                                  boost::log::keywords::max_size = max_size,
                                  boost::log::keywords::format = "[%TimeStamp% %Severity%]: %Message%");
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
wallet_init (false)
{
}

bool logos::node_init::error ()
{
    return block_store_init || wallet_init;
}

logos::node_config::node_config () :
node_config (logos::network::node_port, logos::logging ())
{
}

//#define _DEBUG 1 // Enable to get unit test going...
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
bootstrap_connections (4),
bootstrap_connections_max (64),
callback_port (0),
lmdb_max_dbs (128),
state_block_parse_canary (0),
state_block_generate_canary (0)
{
    switch (logos::logos_network)
    {
        case logos::logos_networks::logos_test_network:
//          LOGOS: ARCHIVE NANO
//          -------------------
#ifdef _DEBUG // Needed to get unit test going...
            preconfigured_representatives.push_back (logos::genesis_account);
#endif
            break;
        case logos::logos_networks::logos_beta_network:
#ifdef _DEBUG
            preconfigured_peers.push_back ("logos-beta.logos.network");
            preconfigured_representatives.push_back (logos::account ("C93F714298E6061E549E52BB8885085319BE977B3FE8F03A1B726E9BE4BE38DE"));
            state_block_parse_canary = logos::block_hash ("5005F5283DE8D2DAB0DAC41DE9BD23640F962B4F0EA7D3128C2EA3D78D578E27");
            state_block_generate_canary = logos::block_hash ("FC18E2265FB835E8CF60E63531053A768CEDF5194263B01A5C95574944E4660D");
#endif
            break;
        case logos::logos_networks::logos_live_network:
//            preconfigured_peers.push_back ("logos.logos.network");
//            preconfigured_representatives.push_back (logos::account ("A30E0A32ED41C8607AA9212843392E853FCBCB4E7CB194E35C94F07F91DE59EF"));
//            preconfigured_representatives.push_back (logos::account ("67556D31DDFC2A440BF6147501449B4CB9572278D034EE686A6BEE29851681DF"));
//            preconfigured_representatives.push_back (logos::account ("5C2FBB148E006A8E8BA7A75DD86C9FE00C83F5FFDBFD76EAA09531071436B6AF"));
//            preconfigured_representatives.push_back (logos::account ("AE7AC63990DAAAF2A69BF11C913B928844BF5012355456F2F164166464024B29"));
//            preconfigured_representatives.push_back (logos::account ("BD6267D6ECD8038327D2BCC0850BDF8F56EC0414912207E81BCF90DFAC8A4AAA"));
//            preconfigured_representatives.push_back (logos::account ("2399A083C600AA0572F5E36247D978FCFC840405F8D4B6D33161C0066A55F431"));
//            preconfigured_representatives.push_back (logos::account ("2298FAB7C61058E77EA554CB93EDEEDA0692CBFCC540AB213B2836B29029E23A"));
//            preconfigured_representatives.push_back (logos::account ("3FE80B4BC842E82C1C18ABFEEC47EA989E63953BC82AC411F304D13833D52A56"));
//            state_block_parse_canary = logos::block_hash ("89F1C0AC4C5AD23964AB880571E3EA67FDC41BD11AB20E67F0A29CF94CD4E24A");
//            state_block_generate_canary = logos::block_hash ("B6DC4D64801BEC7D81DAA086A5733D251E8CBA0E9226FD6173D97C0569EC2998");
            break;
        default:
            assert (false);
            break;
    }
}

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
    tree_a.put ("bootstrap_connections", bootstrap_connections);
    tree_a.put ("bootstrap_connections_max", bootstrap_connections_max);
    tree_a.put ("callback_address", callback_address);
    tree_a.put ("callback_port", std::to_string (callback_port));
    tree_a.put ("callback_target", callback_target);
    tree_a.put ("lmdb_max_dbs", lmdb_max_dbs);
    tree_a.put ("state_block_parse_canary", state_block_parse_canary.to_string ());
    tree_a.put ("state_block_generate_canary", state_block_generate_canary.to_string ());

    boost::property_tree::ptree consensus_manager;
    consensus_manager_config.SerializeJson(consensus_manager);
    tree_a.add_child("ConsensusManager", consensus_manager);
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

        result |= consensus_manager_config.DeserializeJson(tree_a.get_child("ConsensusManager"));
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

logos::block_processor::block_processor (logos::node & node_a) :
stopped (false),
active (false),
node (node_a),
next_log (std::chrono::steady_clock::now ())
{
}

logos::block_processor::~block_processor ()
{
    stop ();
}

void logos::block_processor::stop ()
{
    std::lock_guard<std::mutex> lock (mutex);
    stopped = true;
    condition.notify_all ();
}

void logos::block_processor::flush ()
{
    std::unique_lock<std::mutex> lock (mutex);
    while (!stopped && (!blocks.empty () || active))
    {
        condition.wait (lock);
    }
}

void logos::block_processor::add (std::shared_ptr<logos::block> block_a)
{
    if (!logos::work_validate (block_a->root (), block_a->block_work ()))
    {
        std::lock_guard<std::mutex> lock (mutex);
        blocks.push_front (block_a);
        condition.notify_all ();
    }
    else
    {
        BOOST_LOG (node.log) << "logos::block_processor::add called for hash " << block_a->hash ().to_string () << " with invalid work " << logos::to_string_hex (block_a->block_work ());
        assert (false && "logos::block_processor::add called with invalid work");
    }
}

void logos::block_processor::force (std::shared_ptr<logos::block> block_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    forced.push_front (block_a);
    condition.notify_all ();
}

void logos::block_processor::process_blocks ()
{
    std::unique_lock<std::mutex> lock (mutex);
    while (!stopped)
    {
        if (have_blocks ())
        {
            active = true;
            lock.unlock ();
            process_receive_many (lock);
            lock.lock ();
            active = false;
        }
        else
        {
            condition.notify_all ();
            condition.wait (lock);
        }
    }
}

bool logos::block_processor::should_log ()
{
    auto result (false);
    auto now (std::chrono::steady_clock::now ());
    if (next_log < now)
    {
        next_log = now + std::chrono::seconds (15);
        result = true;
    }
    return result;
}

bool logos::block_processor::have_blocks ()
{
    assert (!mutex.try_lock ());
    return !blocks.empty () || !forced.empty ();
}

void logos::block_processor::process_receive_many (std::unique_lock<std::mutex> & lock_a)
{
    {
        logos::transaction transaction (node.store.environment, nullptr, true);
        auto cutoff (std::chrono::steady_clock::now () + logos::transaction_timeout);
        lock_a.lock ();
        while (have_blocks () && std::chrono::steady_clock::now () < cutoff)
        {
            if (blocks.size () > 64 && should_log ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("%1% blocks in processing queue") % blocks.size ());
            }
            std::shared_ptr<logos::block> block;
            bool force (false);
            if (forced.empty ())
            {
                block = blocks.front ();
                blocks.pop_front ();
            }
            else
            {
                block = forced.front ();
                forced.pop_front ();
                force = true;
            }
            lock_a.unlock ();
            auto hash (block->hash ());
            if (force)
            {
                auto successor (node.ledger.successor (transaction, block->root ()));
                if (successor != nullptr && successor->hash () != hash)
                {
                    // Replace our block with the winner and roll back any dependent blocks
                    BOOST_LOG (node.log) << boost::str (boost::format ("Rolling back %1% and replacing with %2%") % successor->hash ().to_string () % hash.to_string ());
                    //CH There is no more forks comment this line out. most of the logic here will no longer be needed
                    //node.ledger.rollback (transaction, successor->hash ());
                }
            }
            auto process_result (process_receive_one (transaction, block));
            (void)process_result;
            lock_a.lock ();
        }
    }
    lock_a.unlock ();
}

logos::process_return logos::block_processor::process_receive_one (MDB_txn * transaction_a, std::shared_ptr<logos::block> block_a)
{
    //CH block_processor might no longer be needed but I am leaving it here in case we have a need at a later on. all requests are processed within consensus manager right now.
    logos::process_return result;
    auto hash (block_a->hash ());
    result = node.ledger.process (transaction_a, *block_a);
    switch (result.code)
    {
        case logos::process_result::progress:
        {
            if (node.config.logging.ledger_logging ())
            {
                std::string block;
                block_a->serialize_json (block);
                BOOST_LOG (node.log) << boost::str (boost::format ("Processing block %1%: %2%") % hash.to_string () % block);
            }
            if (node.block_arrival.recent (hash))
            {
                //node.active.start (block_a);
            }
            queue_unchecked (transaction_a, hash);
            break;
        }
        case logos::process_result::gap_previous:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Gap previous for: %1%") % hash.to_string ());
            }
            node.store.unchecked_put (transaction_a, block_a->previous (), block_a);
            node.gap_cache.add (transaction_a, block_a);
            break;
        }
        case logos::process_result::gap_source:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Gap source for: %1%") % hash.to_string ());
            }
            node.store.unchecked_put (transaction_a, node.ledger.block_source (transaction_a, *block_a), block_a);
            node.gap_cache.add (transaction_a, block_a);
            break;
        }
        case logos::process_result::state_block_disabled:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("State blocks are disabled: %1%") % hash.to_string ());
            }
            node.store.unchecked_put (transaction_a, node.ledger.state_block_parse_canary, block_a);
            node.gap_cache.add (transaction_a, block_a);
            break;
        }
        case logos::process_result::old:
        {
            if (node.config.logging.ledger_duplicate_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Old for: %1%") % block_a->hash ().to_string ());
            }
            queue_unchecked (transaction_a, hash);
            break;
        }
        case logos::process_result::bad_signature:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Bad signature for: %1%") % hash.to_string ());
            }
            break;
        }
        case logos::process_result::negative_spend:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Negative spend for: %1%") % hash.to_string ());
            }
            break;
        }
        case logos::process_result::unreceivable:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Unreceivable for: %1%") % hash.to_string ());
            }
            break;
        }
        case logos::process_result::not_receive_from_send:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Not receive from send for: %1%") % hash.to_string ());
            }
            break;
        }
        case logos::process_result::fork:
        {
            if (!node.block_arrival.recent (hash))
            {
                // Only let the bootstrap attempt know about forked blocks that did not arrive via UDP.
                node.bootstrap_initiator.process_fork (transaction_a, block_a);
            }
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Fork for: %1% root: %2%") % hash.to_string () % block_a->root ().to_string ());
            }
            break;
        }
        case logos::process_result::account_mismatch:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Account mismatch for: %1%") % hash.to_string ());
            }
            break;
        }
        case logos::process_result::opened_burn_account:
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("*** Rejecting open block for burn account ***: %1%") % hash.to_string ());
            break;
        }
        case logos::process_result::balance_mismatch:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Balance mismatch for: %1%") % hash.to_string ());
            }
            break;
        }
        case logos::process_result::block_position:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("Block %1% cannot follow predecessor %2%") % hash.to_string () % block_a->previous ().to_string ());
            }
            break;
        }

        // Logos - added new process_result values not used
        // by the original rai code. But they shouldn't occur
        // here.
        default:
        {
            if (node.config.logging.ledger_logging ())
            {
                BOOST_LOG (node.log) << "Received unhandled process_result";
            }
            break;
        }
    }
    return result;
}

void logos::block_processor::queue_unchecked (MDB_txn * transaction_a, logos::block_hash const & hash_a)
{
    auto cached (node.store.unchecked_get (transaction_a, hash_a));
    for (auto i (cached.begin ()), n (cached.end ()); i != n; ++i)
    {
        node.store.unchecked_del (transaction_a, hash_a, **i);
        add (*i);
    }
    std::lock_guard<std::mutex> lock (node.gap_cache.mutex);
    node.gap_cache.blocks.get<1> ().erase (hash_a);
}

logos::node::node (logos::node_init & init_a, boost::asio::io_service & service_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, logos::alarm & alarm_a, logos::logging const & logging_a, logos::work_pool & work_a) :
node (init_a, service_a, application_path_a, alarm_a, logos::node_config (peering_port_a, logging_a), work_a)
{
}

logos::node::node (logos::node_init & init_a, boost::asio::io_service & service_a, boost::filesystem::path const & application_path_a, logos::alarm & alarm_a, logos::node_config const & config_a, logos::work_pool & work_a) :
service (service_a),
config (config_a),
alarm (alarm_a),
work (work_a),
store (init_a.block_store_init, application_path_a / "data.ldb", config_a.lmdb_max_dbs),
gap_cache (*this),
ledger (store, stats, config.state_block_parse_canary, config.state_block_generate_canary),
network (*this, config.peering_port),
_validator(new BatchBlock::validator(this)),
bootstrap_initiator (*this),
bootstrap (service_a, config.peering_port, *this),
peers (network.endpoint ()),
application_path (application_path_a),
wallets (init_a.block_store_init, *this),
port_mapping (*this),
warmed_up (0),
block_processor (*this),
block_processor_thread ([this]() { this->block_processor.process_blocks (); }),
stats (config.stat_config),
_recall_handler(),
_identity_manager(store, config.consensus_manager_config),
_archiver(alarm_a, store, _recall_handler)
#ifdef _PRODUCTION
,_consensus_container(service_a, store, alarm_a, config.consensus_manager_config, _archiver, _identity_manager)
#endif
{
    BlocksCallback::Instance(service_a, config.callback_address, config.callback_port, config.callback_target, config.logging.callback_logging ());
// Used to modify the database file with the new account_info field.
// TODO: remove eventually - can be reused for now
//    std::ifstream infile("/home/ubuntu/Downloads/blocks3200_accounts");
//    std::string line;
//    {
//        logos::transaction transaction(store.environment, nullptr, true);
//        while (std::getline(infile, line))
//        {
//            account a(line);
//            //std::cout << "account: " << a.to_string() << std::endl;
//
//            account_info info;
//            if(store.account_get(a, info))
//            {
//                std::cout << "FAILED TO FIND ACCOUNT: " << a.to_string() << std::endl;
//                continue;
//            }
//            if(info.balance.number() > 0)
//                std::cout << "Account balance: " << info.balance.number() << std::endl;
//            //store.account_put(a, info, transaction);
//            // process pair (a,b)
//        }
//    }
//    exit(0);

    wallets.observer = [this](bool active) {
        observers.wallet (active);
    };
    peers.peer_observer = [this](logos::endpoint const & endpoint_a) {
        observers.endpoint (endpoint_a);
    };
    peers.disconnect_observer = [this]() {
        observers.disconnect ();
    };
    observers.blocks.add ([this](std::shared_ptr<logos::block> block_a, logos::account const & account_a, logos::amount const & amount_a, bool is_state_send_a) {
        if (this->block_arrival.recent (block_a->hash ()))
        {
            auto node_l (shared_from_this ());
            background ([node_l, block_a, account_a, amount_a, is_state_send_a]() {
                if (!node_l->config.callback_address.empty ())
                {
                    boost::property_tree::ptree event;
                    event.add ("account", account_a.to_account ());
                    event.add ("hash", block_a->hash ().to_string ());
                    std::string block_text;
                    block_a->serialize_json (block_text);
                    event.add ("block", block_text);
                    event.add ("amount", amount_a.to_string_dec ());
                    if (is_state_send_a)
                    {
                        event.add ("is_send", is_state_send_a);
                    }
                    std::stringstream ostream;
                    boost::property_tree::write_json (ostream, event);
                    ostream.flush ();
                    auto body (std::make_shared<std::string> (ostream.str ()));
                    auto address (node_l->config.callback_address);
                    auto port (node_l->config.callback_port);
                    auto target (std::make_shared<std::string> (node_l->config.callback_target));
                    auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (node_l->service));
                    resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [node_l, address, port, target, body, resolver](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
                        if (!ec)
                        {
                            for (auto i (i_a), n (boost::asio::ip::tcp::resolver::iterator{}); i != n; ++i)
                            {
                                auto sock (std::make_shared<boost::asio::ip::tcp::socket> (node_l->service));
                                sock->async_connect (i->endpoint (), [node_l, target, body, sock, address, port](boost::system::error_code const & ec) {
                                    if (!ec)
                                    {
                                        auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
                                        req->method (boost::beast::http::verb::post);
                                        req->target (*target);
                                        req->version (11);
                                        req->insert (boost::beast::http::field::host, address);
                                        req->insert (boost::beast::http::field::content_type, "application/json");
                                        req->body () = *body;
                                        //req->prepare (*req);
                                        //boost::beast::http::prepare(req);
                                        req->prepare_payload ();
                                        boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req](boost::system::error_code const & ec, size_t bytes_transferred) {
                                            if (!ec)
                                            {
                                                auto sb (std::make_shared<boost::beast::flat_buffer> ());
                                                auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
                                                boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port](boost::system::error_code const & ec, size_t bytes_transferred) {
                                                    if (!ec)
                                                    {
                                                        if (resp->result () == boost::beast::http::status::ok)
                                                        {
                                                        }
                                                        else
                                                        {
                                                            if (node_l->config.logging.callback_logging ())
                                                            {
                                                                BOOST_LOG (node_l->log) << boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ());
                                                            }
                                                        }
                                                    }
                                                    else
                                                    {
                                                        if (node_l->config.logging.callback_logging ())
                                                        {
                                                            BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ());
                                                        }
                                                    };
                                                });
                                            }
                                            else
                                            {
                                                if (node_l->config.logging.callback_logging ())
                                                {
                                                    BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ());
                                                }
                                            }
                                        });
                                    }
                                    else
                                    {
                                        if (node_l->config.logging.callback_logging ())
                                        {
                                            BOOST_LOG (node_l->log) << boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ());
                                        }
                                    }
                                });
                            }
                        }
                        else
                        {
                            if (node_l->config.logging.callback_logging ())
                            {
                                BOOST_LOG (node_l->log) << boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ());
                            }
                        }
                    });
                }
            });
        }
    });

    BOOST_LOG (log) << "Node starting, version: " << LOGOS_VERSION_MAJOR << "." << LOGOS_VERSION_MINOR;
    BOOST_LOG (log) << boost::str (boost::format ("Work pool running %1% threads") % work.threads.size ());
    if (!init_a.error ())
    {
        if (config.logging.node_lifetime_tracing ())
        {
            BOOST_LOG (log) << "Constructing node";
        }


        logos::transaction transaction (store.environment, nullptr, true);
        if (store.latest_begin (transaction) == store.latest_end ())
        {
            // Store was empty meaning we just created it, add the genesis block
            logos::genesis genesis;
            genesis.initialize (transaction, store);
        }

        // check consensus-prototype account_db
        if(store.account_db_empty())
        {
            auto error (false);

            // Construct genesis open block
            //
            boost::property_tree::ptree tree;
            std::stringstream istream(logos::logos_test_genesis);
            boost::property_tree::read_json(istream, tree);
            state_block logos_genesis_block(error, tree);

            if(error)
            {
                throw std::runtime_error("Failed to initialize Logos genesis block.");
            }

            store.receive_put(logos_genesis_block.hash(),
                              logos_genesis_block,
                              transaction);

            store.account_put(genesis_account,
                              {
                                  /* Head         */ 0,
                                  /* Receive Head */ 0,
                                  /* Rep          */ 0,
                                  /* Open         */ logos_genesis_block.hash(),
                                  /* Amount       */ std::numeric_limits<logos::uint128_t>::max(),
                                  /* Time         */ logos::seconds_since_epoch(),
                                  /* Count        */ 0,
                                  /* Receive      */ 0
                              },
                              transaction);
            _identity_manager.CreateGenesisAccounts(transaction);
        }
    }
    if (logos::logos_network ==logos::logos_networks::logos_live_network)
    {
    //CH    Logos does not need this
    //CH extern const char rai_bootstrap_weights[];
    //CH    extern const size_t rai_bootstrap_weights_size;
        /*logos::bufferstream weight_stream ((const uint8_t *)rai_bootstrap_weights, rai_bootstrap_weights_size);
        logos::uint128_union block_height;
        if (!logos::read (weight_stream, block_height))
        {
            auto max_blocks = (uint64_t)block_height.number ();
            logos::transaction transaction (store.environment, nullptr, false);
            if (ledger.store.block_count (transaction).sum () < max_blocks)
            {
                ledger.bootstrap_weight_max_blocks = max_blocks;
                while (true)
                {
                    logos::account account;
                    if (logos::read (weight_stream, account.bytes))
                    {
                        break;
                    }
                    logos::amount weight;
                    if (logos::read (weight_stream, weight.bytes))
                    {
                        break;
                    }
                    BOOST_LOG (log) << "Using bootstrap rep weight: " << account.to_account () << " -> " << weight.format_balance (Mlgs_ratio, 0, true) << " LGS";
                    ledger.bootstrap_weights[account] = weight.number ();
                }
            }
        }*/
    }

}

logos::node::~node ()
{
    if (config.logging.node_lifetime_tracing ())
    {
        BOOST_LOG (log) << "Destructing node";
    }
    stop ();
    delete _validator;
}

bool logos::node::copy_with_compaction (boost::filesystem::path const & destination_file)
{
    return !mdb_env_copy2 (store.environment.environment,
    destination_file.string ().c_str (), MDB_CP_COMPACT);
}

void logos::node::send_keepalive (logos::endpoint const & endpoint_a)
{
    auto endpoint_l (endpoint_a);
    if (endpoint_l.address ().is_v4 ())
    {
        endpoint_l = logos::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
    }
    assert (endpoint_l.address ().is_v6 ());
    network.send_keepalive (endpoint_l);
}

logos::gap_cache::gap_cache (logos::node & node_a) :
node (node_a)
{
}

void logos::gap_cache::add (MDB_txn * transaction_a, std::shared_ptr<logos::block> block_a)
{
    auto hash (block_a->hash ());
    std::lock_guard<std::mutex> lock (mutex);
    auto existing (blocks.get<1> ().find (hash));
    if (existing != blocks.get<1> ().end ())
    {
        blocks.get<1> ().modify (existing, [](logos::gap_information & info) {
            info.arrival = std::chrono::steady_clock::now ();
        });
    }
    else
    {
        blocks.insert ({ std::chrono::steady_clock::now (), hash, std::unique_ptr<logos::votes> (new logos::votes (block_a)) });
        if (blocks.size () > max)
        {
            blocks.get<0> ().erase (blocks.get<0> ().begin ());
        }
    }
}

void logos::gap_cache::purge_old ()
{
    auto cutoff (std::chrono::steady_clock::now () - std::chrono::seconds (10));
    std::lock_guard<std::mutex> lock (mutex);
    auto done (false);
    while (!done && !blocks.empty ())
    {
        auto first (blocks.get<1> ().begin ());
        if (first->arrival < cutoff)
        {
            blocks.get<1> ().erase (first);
        }
        else
        {
            done = true;
        }
    }
}


void logos::node::process_active (std::shared_ptr<logos::block> incoming)
{
    if (!block_arrival.add (incoming->hash ()))
    {
        block_processor.add (incoming);
    }
}

logos::process_return logos::node::process (logos::block const & block_a)
{
    logos::transaction transaction (store.environment, nullptr, true);
    auto result (ledger.process (transaction, block_a));
    return result;
}

// Simulating with sqrt_broadcast_simulate shows we only need to broadcast to sqrt(total_peers) random peers in order to successfully publish to everyone with high probability
std::deque<logos::endpoint> logos::peer_container::list_fanout ()
{
    auto peers (random_set (size_sqrt ()));
    std::deque<logos::endpoint> result;
    for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
    {
        result.push_back (*i);
    }
    return result;
}

std::deque<logos::endpoint> logos::peer_container::list ()
{
    std::deque<logos::endpoint> result;
    std::lock_guard<std::mutex> lock (mutex);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        result.push_back (i->endpoint);
    }
    std::random_shuffle (result.begin (), result.end ());
    return result;
}

std::map<logos::endpoint, unsigned> logos::peer_container::list_version ()
{
    std::map<logos::endpoint, unsigned> result;
    std::lock_guard<std::mutex> lock (mutex);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        result.insert (std::pair<logos::endpoint, unsigned> (i->endpoint, i->network_version));
    }
    return result;
}

logos::endpoint logos::peer_container::bootstrap_peer ()
{
    logos::endpoint result (boost::asio::ip::address_v6::any (), 0);
    std::lock_guard<std::mutex> lock (mutex);
    for (auto i (peers.get<4> ().begin ()), n (peers.get<4> ().end ()); i != n;)
    {
        if (i->network_version >= 0x5)
        {
            result = i->endpoint;
            peers.get<4> ().modify (i, [](logos::peer_information & peer_a) {
                peer_a.last_bootstrap_attempt = std::chrono::steady_clock::now ();
            });
            i = n;
        }
        else
        {
            ++i;
        }
    }
    return result;
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

bool logos::parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
    auto result (false);
    auto port_position (string.rfind (':'));
    if (port_position != std::string::npos && port_position > 0)
    {
        std::string port_string (string.substr (port_position + 1));
        try
        {
            uint16_t port;
            result = parse_port (port_string, port);
            if (!result)
            {
                boost::system::error_code ec;
                auto address (boost::asio::ip::address_v6::from_string (string.substr (0, port_position), ec));
                if (!ec)
                {
                    address_a = address;
                    port_a = port;
                }
                else
                {
                    result = true;
                }
            }
            else
            {
                result = true;
            }
        }
        catch (...)
        {
            result = true;
        }
    }
    else
    {
        result = true;
    }
    return result;
}

bool logos::parse_endpoint (std::string const & string, logos::endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = logos::endpoint (address, port);
    }
    return result;
}

bool logos::parse_tcp_endpoint (std::string const & string, logos::tcp_endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = logos::tcp_endpoint (address, port);
    }
    return result;
}

void logos::node::start ()
{
//  LOGOS: ARCHIVE
//  -------------------

//#ifdef _DEBUG
    network.receive (); // Needed to get unit test going...
    ongoing_keepalive ();
    ongoing_bootstrap ();
    ongoing_store_flush ();
    //ongoing_rep_crawl ();
    bootstrap.start ();
    backup_wallet ();
    //active.announce_votes ();
    //online_reps.recalculate_stake ();
    port_mapping.start ();
    add_initial_peers ();
    observers.started (); // Seems to cause consensus to fail...
//#endif

// CH added starting logic here instead of inside constructors
#ifdef _PRODUCTION
    _archiver.Start(_consensus_container);
#endif
}

void logos::node::stop ()
{
    BOOST_LOG (log) << "Node stopping";
    block_processor.stop ();
    if (block_processor_thread.joinable ())
    {
        block_processor_thread.join ();
    }
    //CH active.stop ();
    network.stop ();
    bootstrap_initiator.stop ();
    bootstrap.stop ();
    port_mapping.stop ();
    wallets.stop ();
}

void logos::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
    for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
    {
        keepalive (*i, logos::network::node_port);
    }
}

logos::block_hash logos::node::latest (logos::account const & account_a)
{
    logos::transaction transaction (store.environment, nullptr, false);
    return ledger.latest (transaction, account_a);
}

logos::uint128_t logos::node::balance (logos::account const & account_a)
{
    logos::transaction transaction (store.environment, nullptr, false);
    return ledger.account_balance (transaction, account_a);
}

std::unique_ptr<logos::block> logos::node::block (logos::block_hash const & hash_a)
{
    logos::transaction transaction (store.environment, nullptr, false);
    return store.block_get (transaction, hash_a);
}

std::pair<logos::uint128_t, logos::uint128_t> logos::node::balance_pending (logos::account const & account_a)
{
    std::pair<logos::uint128_t, logos::uint128_t> result;
    logos::transaction transaction (store.environment, nullptr, false);
    result.first = ledger.account_balance (transaction, account_a);
    result.second = ledger.account_pending (transaction, account_a);
    return result;
}

logos::uint128_t logos::node::weight (logos::account const & account_a)
{
    logos::transaction transaction (store.environment, nullptr, false);
    return ledger.weight (transaction, account_a);
}

logos::account logos::node::representative (logos::account const & account_a)
{
    logos::transaction transaction (store.environment, nullptr, false);
    logos::account_info info;
    logos::account result (0);
    if (!store.account_get (transaction, account_a, info))
    {
        result = info.rep_block;
    }
    return result;
}

void logos::node::ongoing_keepalive ()
{
    keepalive_preconfigured (config.preconfigured_peers);
    auto peers_l (peers.purge_list (std::chrono::steady_clock::now () - cutoff));
    for (auto i (peers_l.begin ()), j (peers_l.end ()); i != j && std::chrono::steady_clock::now () - i->last_attempt > period; ++i)
    {
        network.send_keepalive (i->endpoint);
    }
    std::weak_ptr<logos::node> node_w (shared_from_this ());
    alarm.add (std::chrono::steady_clock::now () + period, [node_w]() {
        if (auto node_l = node_w.lock ())
        {
            node_l->ongoing_keepalive ();
        }
    });
}

void logos::node::ongoing_bootstrap ()
{
    auto next_wakeup (300);
    if (warmed_up < 3)
    {
        // Re-attempt bootstrapping more aggressively on startup
        next_wakeup = 5;
        if (!bootstrap_initiator.in_progress () && !peers.empty ())
        {
            ++warmed_up;
        }
    }
    bootstrap_initiator.bootstrap ();
    std::weak_ptr<logos::node> node_w (shared_from_this ());
    alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (next_wakeup), [node_w]() {
        if (auto node_l = node_w.lock ())
        {
            node_l->ongoing_bootstrap ();
        }
    });
}

void logos::node::ongoing_store_flush ()
{
    {
        logos::transaction transaction (store.environment, nullptr, true);
        store.flush (transaction);
    }
    std::weak_ptr<logos::node> node_w (shared_from_this ());
    alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w]() {
        if (auto node_l = node_w.lock ())
        {
            node_l->ongoing_store_flush ();
        }
    });
}

void logos::node::backup_wallet ()
{
    logos::transaction transaction (store.environment, nullptr, false);
    for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
    {
        auto backup_path (application_path / "backup");
        boost::filesystem::create_directories (backup_path);
        i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
    }
    auto this_l (shared ());
    alarm.add (std::chrono::steady_clock::now () + backup_interval, [this_l]() {
        this_l->backup_wallet ();
    });
}

int logos::node::price (logos::uint128_t const & balance_a, int amount_a)
{
    assert (balance_a >= amount_a * logos::Glgs_ratio);
    auto balance_l (balance_a);
    double result (0.0);
    for (auto i (0); i < amount_a; ++i)
    {
        balance_l -= logos::Glgs_ratio;
        auto balance_scaled ((balance_l / logos::Mlgs_ratio).convert_to<double> ());
        auto units (balance_scaled / 1000.0);
        auto unit_price (((free_cutoff - units) / free_cutoff) * price_max);
        result += std::min (std::max (0.0, unit_price), price_max);
    }
    return static_cast<int> (result * 100.0);
}

namespace
{
class work_request
{
public:
    work_request (boost::asio::io_service & service_a, boost::asio::ip::address address_a, uint16_t port_a) :
    address (address_a),
    port (port_a),
    socket (service_a)
    {
    }
    boost::asio::ip::address address;
    uint16_t port;
    boost::beast::flat_buffer buffer;
    boost::beast::http::response<boost::beast::http::string_body> response;
    boost::asio::ip::tcp::socket socket;
};
class distributed_work : public std::enable_shared_from_this<distributed_work>
{
public:
    distributed_work (std::shared_ptr<logos::node> const & node_a, logos::block_hash const & root_a, std::function<void(uint64_t)> callback_a, unsigned int backoff_a = 1) :
    callback (callback_a),
    node (node_a),
    root (root_a),
    backoff (backoff_a),
    need_resolve (node_a->config.work_peers)
    {
        completed.clear ();
    }
    void start ()
    {
        if (need_resolve.empty ())
        {
            start_work ();
        }
        else
        {
            auto current (need_resolve.back ());
            need_resolve.pop_back ();
            auto this_l (shared_from_this ());
            boost::system::error_code ec;
            auto parsed_address (boost::asio::ip::address_v6::from_string (current.first, ec));
            if (!ec)
            {
                outstanding[parsed_address] = current.second;
                start ();
            }
            else
            {
                node->network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (current.first, std::to_string (current.second)), [current, this_l](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
                    if (!ec)
                    {
                        for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
                        {
                            auto endpoint (i->endpoint ());
                            this_l->outstanding[endpoint.address ()] = endpoint.port ();
                        }
                    }
                    else
                    {
                        BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Error resolving work peer: %1%:%2%: %3%") % current.first % current.second % ec.message ());
                    }
                    this_l->start ();
                });
            }
        }
    }
    void start_work ()
    {
        if (!outstanding.empty ())
        {
            auto this_l (shared_from_this ());
            std::lock_guard<std::mutex> lock (mutex);
            for (auto const & i : outstanding)
            {
                auto host (i.first);
                auto service (i.second);
                node->background ([this_l, host, service]() {
                    auto connection (std::make_shared<work_request> (this_l->node->service, host, service));
                    connection->socket.async_connect (logos::tcp_endpoint (host, service), [this_l, connection](boost::system::error_code const & ec) {
                        if (!ec)
                        {
                            std::string request_string;
                            {
                                boost::property_tree::ptree request;
                                request.put ("action", "work_generate");
                                request.put ("hash", this_l->root.to_string ());
                                std::stringstream ostream;
                                boost::property_tree::write_json (ostream, request);
                                request_string = ostream.str ();
                            }
                            auto request (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
                            request->method (boost::beast::http::verb::post);
                            request->target ("/");
                            request->version (11);
                            request->body () = request_string;
                            request->prepare_payload ();
                            boost::beast::http::async_write (connection->socket, *request, [this_l, connection, request](boost::system::error_code const & ec, size_t bytes_transferred) {
                                if (!ec)
                                {
                                    boost::beast::http::async_read (connection->socket, connection->buffer, connection->response, [this_l, connection](boost::system::error_code const & ec, size_t bytes_transferred) {
                                        if (!ec)
                                        {
                                            if (connection->response.result () == boost::beast::http::status::ok)
                                            {
                                                this_l->success (connection->response.body (), connection->address);
                                            }
                                            else
                                            {
                                                BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Work peer responded with an error %1% %2%: %3%") % connection->address % connection->port % connection->response.result ());
                                                this_l->failure (connection->address);
                                            }
                                        }
                                        else
                                        {
                                            BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to read from work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
                                            this_l->failure (connection->address);
                                        }
                                    });
                                }
                                else
                                {
                                    BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to write to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
                                    this_l->failure (connection->address);
                                }
                            });
                        }
                        else
                        {
                            BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Unable to connect to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ());
                            this_l->failure (connection->address);
                        }
                    });
                });
            }
        }
        else
        {
            handle_failure (true);
        }
    }
    void stop ()
    {
        auto this_l (shared_from_this ());
        std::lock_guard<std::mutex> lock (mutex);
        for (auto const & i : outstanding)
        {
            auto host (i.first);
            auto service (i.second);
            node->background ([this_l, host, service]() {
                std::string request_string;
                {
                    boost::property_tree::ptree request;
                    request.put ("action", "work_cancel");
                    request.put ("hash", this_l->root.to_string ());
                    std::stringstream ostream;
                    boost::property_tree::write_json (ostream, request);
                    request_string = ostream.str ();
                }
                boost::beast::http::request<boost::beast::http::string_body> request;
                request.method (boost::beast::http::verb::post);
                request.target ("/");
                request.version (11);
                request.body () = request_string;
                request.prepare_payload ();
                auto socket (std::make_shared<boost::asio::ip::tcp::socket> (this_l->node->service));
                boost::beast::http::async_write (*socket, request, [socket](boost::system::error_code const & ec, size_t bytes_transferred) {
                });
            });
        }
        outstanding.clear ();
    }
    void success (std::string const & body_a, boost::asio::ip::address const & address)
    {
        auto last (remove (address));
        std::stringstream istream (body_a);
        try
        {
            boost::property_tree::ptree result;
            boost::property_tree::read_json (istream, result);
            auto work_text (result.get<std::string> ("work"));
            uint64_t work;
            if (!logos::from_string_hex (work_text, work))
            {
                if (!logos::work_validate (root, work))
                {
                    set_once (work);
                    stop ();
                }
                else
                {
                    BOOST_LOG (node->log) << boost::str (boost::format ("Incorrect work response from %1% for root %2%: %3%") % address % root.to_string () % work_text);
                    handle_failure (last);
                }
            }
            else
            {
                BOOST_LOG (node->log) << boost::str (boost::format ("Work response from %1% wasn't a number: %2%") % address % work_text);
                handle_failure (last);
            }
        }
        catch (...)
        {
            BOOST_LOG (node->log) << boost::str (boost::format ("Work response from %1% wasn't parsable: %2%") % address % body_a);
            handle_failure (last);
        }
    }
    void set_once (uint64_t work_a)
    {
        if (!completed.test_and_set ())
        {
            callback (work_a);
        }
    }
    void failure (boost::asio::ip::address const & address)
    {
        auto last (remove (address));
        handle_failure (last);
    }
    void handle_failure (bool last)
    {
        if (last)
        {
            if (!completed.test_and_set ())
            {
                if (node->config.work_threads != 0 || node->work.opencl)
                {
                    auto callback_l (callback);
                    node->work.generate (root, [callback_l](boost::optional<uint64_t> const & work_a) {
                        callback_l (work_a.value ());
                    });
                }
                else
                {
                    if (backoff == 1 && node->config.logging.work_generation_time ())
                    {
                        BOOST_LOG (node->log) << "Work peer(s) failed to generate work for root " << root.to_string () << ", retrying...";
                    }
                    auto now (std::chrono::steady_clock::now ());
                    auto root_l (root);
                    auto callback_l (callback);
                    std::weak_ptr<logos::node> node_w (node);
                    auto next_backoff (std::min (backoff * 2, (unsigned int)60 * 5));
                    node->alarm.add (now + std::chrono::seconds (backoff), [node_w, root_l, callback_l, next_backoff] {
                        if (auto node_l = node_w.lock ())
                        {
                            auto work_generation (std::make_shared<distributed_work> (node_l, root_l, callback_l, next_backoff));
                            work_generation->start ();
                        }
                    });
                }
            }
        }
    }
    bool remove (boost::asio::ip::address const & address)
    {
        std::lock_guard<std::mutex> lock (mutex);
        outstanding.erase (address);
        return outstanding.empty ();
    }
    std::function<void(uint64_t)> callback;
    unsigned int backoff; // in seconds
    std::shared_ptr<logos::node> node;
    logos::block_hash root;
    std::mutex mutex;
    std::map<boost::asio::ip::address, uint16_t> outstanding;
    std::vector<std::pair<std::string, uint16_t>> need_resolve;
    std::atomic_flag completed;
};
}

void logos::node::work_generate_blocking (logos::block & block_a)
{
    block_a.block_work_set (work_generate_blocking (block_a.root ()));
}

void logos::node::work_generate (logos::uint256_union const & hash_a, std::function<void(uint64_t)> callback_a)
{
    auto work_generation (std::make_shared<distributed_work> (shared (), hash_a, callback_a));
    work_generation->start ();
}

uint64_t logos::node::work_generate_blocking (logos::uint256_union const & hash_a)
{
    std::promise<uint64_t> promise;
    work_generate (hash_a, [&promise](uint64_t work_a) {
        promise.set_value (work_a);
    });
    return promise.get_future ().get ();
}

void logos::node::add_initial_peers ()
{
#ifdef _PRODUCTION
    LOG_DEBUG(log) << "logos::node::add_initial_peers: ";
    // Add our peers from the configuation...
    //uint32_t port = 70003; // What port for bootstrapping ???
    uint32_t port = config.peering_port; // What port for bootstrapping ???
    for(int i = 0; i < config.consensus_manager_config.delegates.size(); ++i) {
#if 0
        logos::endpoint peer = logos::endpoint(
            boost::asio::ip::address::from_string(
                (std::string("::") + config.consensus_manager_config.delegates[i].ip)) , port );
#endif
        boost::asio::ip::address_v4 v4 = boost::asio::ip::make_address_v4(config.consensus_manager_config.delegates[i].ip);
        boost::asio::ip::address_v6 v6 = boost::asio::ip::make_address_v6(boost::asio::ip::v4_mapped,v4);
        std::cout << " v6: " << v6.to_string() << std::endl;
        logos::endpoint peer = logos::endpoint(
            boost::asio::ip::address::from_string(
                v6.to_string()), port );

        LOG_DEBUG(log) << "adding peer: " << config.consensus_manager_config.delegates[i].ip << std::endl;
        try {
            if(peers.insert( peer, logos::protocol_version )) {
                LOG_DEBUG(log) << "error adding peer: " << config.consensus_manager_config.delegates[i].ip << std::endl;
            }
        } catch(boost::asio::ip::bad_address_cast &e) {
            LOG_DEBUG(log) << " failed to add peer: " << config.consensus_manager_config.delegates[i].ip << " reason: " << e.what() << std::endl;
        } catch(...) {
            LOG_DEBUG(log) << " failed to add peer: " << config.consensus_manager_config.delegates[i].ip << std::endl;
        }
    }
#endif
}



logos::process_return logos::node::OnSendRequest(std::shared_ptr<logos::state_block> block, bool should_buffer)
{
    process_return result;
#ifndef _PRODUCTION
    return result;
#else
    return _consensus_container.OnSendRequest(block, should_buffer);
#endif
}

logos::process_return logos::node::BufferComplete()
{
    process_return result;

#ifdef _PRODUCTION
    _consensus_container.BufferComplete(result);
#endif

    return result;
}

void logos::node::process_message (logos::message & message_a, logos::endpoint const & sender_a)
{
    network_message_visitor visitor (*this, sender_a);
    message_a.visit (visitor);
}

logos::endpoint logos::network::endpoint ()
{
    boost::system::error_code ec;
    auto port (socket.local_endpoint (ec).port ());
    if (ec)
    {
        BOOST_LOG (node.log) << "Unable to retrieve port: " << ec.message ();
    }
    return logos::endpoint (boost::asio::ip::address_v6::loopback (), port);
}

bool logos::block_arrival::add (logos::block_hash const & hash_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    auto now (std::chrono::steady_clock::now ());
    auto inserted (arrival.insert (logos::block_arrival_info{ now, hash_a }));
    auto result (!inserted.second);
    return result;
}

bool logos::block_arrival::recent (logos::block_hash const & hash_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    auto now (std::chrono::steady_clock::now ());
    while (arrival.size () > arrival_size_min && arrival.begin ()->arrival + arrival_time_min < now)
    {
        arrival.erase (arrival.begin ());
    }
    return arrival.get<1> ().find (hash_a) != arrival.get<1> ().end ();
}

std::unordered_set<logos::endpoint> logos::peer_container::random_set (size_t count_a)
{
    std::unordered_set<logos::endpoint> result;
    result.reserve (count_a);
    std::lock_guard<std::mutex> lock (mutex);
    // Stop trying to fill result with random samples after this many attempts
    auto random_cutoff (count_a * 2);
    auto peers_size (peers.size ());
    // Usually count_a will be much smaller than peers.size()
    // Otherwise make sure we have a cutoff on attempting to randomly fill
    if (!peers.empty ())
    {
        for (auto i (0); i < random_cutoff && result.size () < count_a; ++i)
        {
            auto index (random_pool.GenerateWord32 (0, peers_size - 1));
            result.insert (peers.get<3> ()[index].endpoint);
        }
    }
    // Fill the remainder with most recent contact
    for (auto i (peers.get<1> ().begin ()), n (peers.get<1> ().end ()); i != n && result.size () < count_a; ++i)
    {
        result.insert (i->endpoint);
    }
    return result;
}

void logos::peer_container::random_fill (std::array<logos::endpoint, 8> & target_a)
{
    auto peers (random_set (target_a.size ()));
    assert (peers.size () <= target_a.size ());
    auto endpoint (logos::endpoint (boost::asio::ip::address_v6{}, 0));
    assert (endpoint.address ().is_v6 ());
    std::fill (target_a.begin (), target_a.end (), endpoint);
    auto j (target_a.begin ());
    for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
    {
        assert (i->address ().is_v6 ());
        assert (j < target_a.end ());
        *j = *i;
    }
}

// Request a list of the top known representatives
std::vector<logos::peer_information> logos::peer_container::representatives (size_t count_a)
{
    std::vector<peer_information> result;
    result.reserve (std::min (count_a, size_t (16)));
    std::lock_guard<std::mutex> lock (mutex);
    for (auto i (peers.get<6> ().begin ()), n (peers.get<6> ().end ()); i != n && result.size () < count_a; ++i)
    {
        if (!i->rep_weight.is_zero ())
        {
            result.push_back (*i);
        }
    }
    return result;
}

std::vector<logos::peer_information> logos::peer_container::purge_list (std::chrono::steady_clock::time_point const & cutoff)
{
    std::vector<logos::peer_information> result;
    {
        std::lock_guard<std::mutex> lock (mutex);
        //auto pivot (peers.get<1> ().lower_bound (cutoff - std::chrono::hours(24))); // Disable cut-off for testing...
        auto pivot (peers.get<1> ().lower_bound (cutoff));
        result.assign (pivot, peers.get<1> ().end ());
        // Remove peers that haven't been heard from past the cutoff
        peers.get<1> ().erase (peers.get<1> ().begin (), pivot);
        for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
        {
            peers.modify (i, [](logos::peer_information & info) { info.last_attempt = std::chrono::steady_clock::now (); });
        }

        // Remove keepalive attempt tracking for attempts older than cutoff
        auto attempts_pivot (attempts.get<1> ().lower_bound (cutoff));
        attempts.get<1> ().erase (attempts.get<1> ().begin (), attempts_pivot);
    }
    if (result.empty ())
    {
        disconnect_observer ();
    }
    return result;
}

size_t logos::peer_container::size ()
{
    std::lock_guard<std::mutex> lock (mutex);
    return peers.size ();
}

size_t logos::peer_container::size_sqrt ()
{
    auto result (std::ceil (std::sqrt (size ())));
    return result;
}

bool logos::peer_container::empty ()
{
    return size () == 0;
}

bool logos::peer_container::not_a_peer (logos::endpoint const & endpoint_a)
{
    bool result (false);
    if (endpoint_a.address ().to_v6 ().is_unspecified ())
    {
        result = true;
    }
    else if (logos::reserved_address (endpoint_a))
    {
        result = true;
    }
    else if (endpoint_a == self)
    {
        result = true;
    }
    return result;
}

bool logos::peer_container::rep_response (logos::endpoint const & endpoint_a, logos::account const & rep_account_a, logos::amount const & weight_a)
{
    auto updated (false);
    std::lock_guard<std::mutex> lock (mutex);
    auto existing (peers.find (endpoint_a));
    if (existing != peers.end ())
    {
        peers.modify (existing, [weight_a, &updated, rep_account_a](logos::peer_information & info) {
            info.last_rep_response = std::chrono::steady_clock::now ();
            if (info.rep_weight < weight_a)
            {
                updated = true;
                info.rep_weight = weight_a;
                info.probable_rep_account = rep_account_a;
            }
        });
    }
    return updated;
}

void logos::peer_container::rep_request (logos::endpoint const & endpoint_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    auto existing (peers.find (endpoint_a));
    if (existing != peers.end ())
    {
        peers.modify (existing, [](logos::peer_information & info) {
            info.last_rep_request = std::chrono::steady_clock::now ();
        });
    }
}

bool logos::peer_container::reachout (logos::endpoint const & endpoint_a)
{
    // Don't contact invalid IPs
    bool error = not_a_peer (endpoint_a);
    if (!error)
    {
        // Don't keepalive to nodes that already sent us something
        error |= known_peer (endpoint_a);
        std::lock_guard<std::mutex> lock (mutex);
        auto existing (attempts.find (endpoint_a));
        error |= existing != attempts.end ();
        attempts.insert ({ endpoint_a, std::chrono::steady_clock::now () });
    }
    return error;
}

bool logos::peer_container::insert (logos::endpoint const & endpoint_a, unsigned version_a)
{
    auto unknown (false);
    auto result (not_a_peer (endpoint_a));
    if (!result)
    {
        if (version_a >= logos::protocol_version_min)
        {
            std::lock_guard<std::mutex> lock (mutex);
            auto existing (peers.find (endpoint_a));
            if (existing != peers.end ())
            {
                peers.modify (existing, [](logos::peer_information & info) {
                    info.last_contact = std::chrono::steady_clock::now ();
                });
                result = true;
            }
            else
            {
                peers.insert (logos::peer_information (endpoint_a, version_a));
                unknown = true;
            }
        }
    }
    if (unknown && !result)
    {
        peer_observer (endpoint_a);
    }
    return result;
}

namespace
{
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long address_a)
{
    return boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (address_a));
}
}

bool logos::reserved_address (logos::endpoint const & endpoint_a)
{
    assert (endpoint_a.address ().is_v6 ());
    auto bytes (endpoint_a.address ().to_v6 ());
    auto result (false);
    static auto const rfc1700_min (mapped_from_v4_bytes (0x00000000ul));
    static auto const rfc1700_max (mapped_from_v4_bytes (0x00fffffful));
    static auto const ipv4_loopback_min (mapped_from_v4_bytes (0x7f000000ul));
    static auto const ipv4_loopback_max (mapped_from_v4_bytes (0x7ffffffful));
    static auto const rfc1918_1_min (mapped_from_v4_bytes (0x0a000000ul));
    static auto const rfc1918_1_max (mapped_from_v4_bytes (0x0afffffful));
    static auto const rfc1918_2_min (mapped_from_v4_bytes (0xac100000ul));
    static auto const rfc1918_2_max (mapped_from_v4_bytes (0xac1ffffful));
    static auto const rfc1918_3_min (mapped_from_v4_bytes (0xc0a80000ul));
    static auto const rfc1918_3_max (mapped_from_v4_bytes (0xc0a8fffful));
    static auto const rfc6598_min (mapped_from_v4_bytes (0x64400000ul));
    static auto const rfc6598_max (mapped_from_v4_bytes (0x647ffffful));
    static auto const rfc5737_1_min (mapped_from_v4_bytes (0xc0000200ul));
    static auto const rfc5737_1_max (mapped_from_v4_bytes (0xc00002fful));
    static auto const rfc5737_2_min (mapped_from_v4_bytes (0xc6336400ul));
    static auto const rfc5737_2_max (mapped_from_v4_bytes (0xc63364fful));
    static auto const rfc5737_3_min (mapped_from_v4_bytes (0xcb007100ul));
    static auto const rfc5737_3_max (mapped_from_v4_bytes (0xcb0071fful));
    static auto const ipv4_multicast_min (mapped_from_v4_bytes (0xe0000000ul));
    static auto const ipv4_multicast_max (mapped_from_v4_bytes (0xeffffffful));
    static auto const rfc6890_min (mapped_from_v4_bytes (0xf0000000ul));
    static auto const rfc6890_max (mapped_from_v4_bytes (0xfffffffful));
    static auto const rfc6666_min (boost::asio::ip::address_v6::from_string ("100::"));
    static auto const rfc6666_max (boost::asio::ip::address_v6::from_string ("100::ffff:ffff:ffff:ffff"));
    static auto const rfc3849_min (boost::asio::ip::address_v6::from_string ("2001:db8::"));
    static auto const rfc3849_max (boost::asio::ip::address_v6::from_string ("2001:db8:ffff:ffff:ffff:ffff:ffff:ffff"));
    static auto const rfc4193_min (boost::asio::ip::address_v6::from_string ("fc00::"));
    static auto const rfc4193_max (boost::asio::ip::address_v6::from_string ("fd00:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
    static auto const ipv6_multicast_min (boost::asio::ip::address_v6::from_string ("ff00::"));
    static auto const ipv6_multicast_max (boost::asio::ip::address_v6::from_string ("ff00:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
    if (bytes >= rfc1700_min && bytes <= rfc1700_max)
    {
        result = true;
    }
    else if (bytes >= rfc5737_1_min && bytes <= rfc5737_1_max)
    {
        result = true;
    }
    else if (bytes >= rfc5737_2_min && bytes <= rfc5737_2_max)
    {
        result = true;
    }
    else if (bytes >= rfc5737_3_min && bytes <= rfc5737_3_max)
    {
        result = true;
    }
    else if (bytes >= ipv4_multicast_min && bytes <= ipv4_multicast_max)
    {
        result = true;
    }
    else if (bytes >= rfc6890_min && bytes <= rfc6890_max)
    {
        result = true;
    }
    else if (bytes >= rfc6666_min && bytes <= rfc6666_max)
    {
        result = true;
    }
    else if (bytes >= rfc3849_min && bytes <= rfc3849_max)
    {
        result = true;
    }
    else if (bytes >= ipv6_multicast_min && bytes <= ipv6_multicast_max)
    {
        result = true;
    }
    else if (logos::logos_network ==logos::logos_networks::logos_live_network)
    {
        if (bytes.is_loopback ())
        {
            result = true;
        }
        else if (bytes >= ipv4_loopback_min && bytes <= ipv4_loopback_max)
        {
            result = true;
        }
        else if (bytes >= rfc1918_1_min && bytes <= rfc1918_1_max)
        {
            result = true;
        }
        else if (bytes >= rfc1918_2_min && bytes <= rfc1918_2_max)
        {
            result = true;
        }
        else if (bytes >= rfc1918_3_min && bytes <= rfc1918_3_max)
        {
            result = true;
        }
        else if (bytes >= rfc6598_min && bytes <= rfc6598_max)
        {
            result = true;
        }
        else if (bytes >= rfc4193_min && bytes <= rfc4193_max)
        {
            result = true;
        }
    }
    return result;
}

logos::peer_information::peer_information (logos::endpoint const & endpoint_a, unsigned network_version_a) :
endpoint (endpoint_a),
last_contact (std::chrono::steady_clock::now ()),
last_attempt (last_contact),
last_bootstrap_attempt (std::chrono::steady_clock::time_point ()),
last_rep_request (std::chrono::steady_clock::time_point ()),
last_rep_response (std::chrono::steady_clock::time_point ()),
rep_weight (0),
network_version (network_version_a)
{
}

logos::peer_information::peer_information (logos::endpoint const & endpoint_a, std::chrono::steady_clock::time_point const & last_contact_a, std::chrono::steady_clock::time_point const & last_attempt_a) :
endpoint (endpoint_a),
last_contact (last_contact_a),
last_attempt (last_attempt_a),
last_bootstrap_attempt (std::chrono::steady_clock::time_point ()),
last_rep_request (std::chrono::steady_clock::time_point ()),
last_rep_response (std::chrono::steady_clock::time_point ()),
rep_weight (0)
{
}

logos::peer_container::peer_container (logos::endpoint const & self_a) :
self (self_a),
peer_observer ([](logos::endpoint const &) {}),
disconnect_observer ([]() {})
{
}

void logos::peer_container::contacted (logos::endpoint const & endpoint_a, unsigned version_a)
{
    auto endpoint_l (endpoint_a);
    if (endpoint_l.address ().is_v4 ())
    {
        endpoint_l = logos::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
    }
    assert (endpoint_l.address ().is_v6 ());
    insert (endpoint_l, version_a);
}

void logos::network::send_buffer (uint8_t const * data_a, size_t size_a, logos::endpoint const & endpoint_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
    std::unique_lock<std::mutex> lock (socket_mutex);
    if (node.config.logging.network_packet_logging ())
    {
        BOOST_LOG (node.log) << "Sending packet";
    }
    socket.async_send_to (boost::asio::buffer (data_a, size_a), endpoint_a, [this, callback_a](boost::system::error_code const & ec, size_t size_a) {
        callback_a (ec, size_a);
        this->node.stats.add (logos::stat::type::traffic, logos::stat::dir::out, size_a);
        if (this->node.config.logging.network_packet_logging ())
        {
            BOOST_LOG (this->node.log) << "Packet send complete";
        }
    });
}

bool logos::peer_container::known_peer (logos::endpoint const & endpoint_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    auto existing (peers.find (endpoint_a));
    return existing != peers.end ();
}

std::shared_ptr<logos::node> logos::node::shared ()
{
    return shared_from_this ();
}

bool logos::vote_info::operator< (logos::vote const & vote_a) const
{
    return sequence < vote_a.sequence || (sequence == vote_a.sequence && hash < vote_a.block->hash ());
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
            catch (const std::runtime_error & e)
            {
                trace();
                std::cerr << "Error while running thread_runner (" << e.what () << ")\n";
            }
            catch (...)
            {
                assert (false && "Unhandled service exception");
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
    if (vm.count ("account_create"))
    {
        if (vm.count ("wallet") == 1)
        {
            logos::uint256_union wallet_id;
            if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
            {
                std::string password;
                if (vm.count ("password") > 0)
                {
                    password = vm["password"].as<std::string> ();
                }
                inactive_node node (data_path);
                auto wallet (node.node->wallets.open (wallet_id));
                if (wallet != nullptr)
                {
                    if (!wallet->enter_password (password))
                    {
                        logos::transaction transaction (wallet->store.environment, nullptr, true);
                        auto pub (wallet->store.deterministic_insert (transaction));
                        std::cout << boost::str (boost::format ("Account: %1%\n") % pub.to_account ());
                    }
                    else
                    {
                        std::cerr << "Invalid password\n";
                        result = true;
                    }
                }
                else
                {
                    std::cerr << "Wallet doesn't exist\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "Invalid wallet id\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option\n";
            result = true;
        }
    }
    else if (vm.count ("account_get") > 0)
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
    else if (vm.count ("diagnostics"))
    {
        inactive_node node (data_path);
        //CH fix later
        //std::cout << "Testing hash function" << std::endl;
        //logos::raw_key key;
        //key.data.clear ();
        //logos::state_block send (0, 0, 0, key, 0, 0);
        std::cout << "Testing key derivation function" << std::endl;
        logos::raw_key junk1;
        junk1.data.clear ();
        logos::uint256_union junk2 (0);
        logos::kdf kdf;
        kdf.phs (junk1, "", junk2);
        std::cout << "Dumping OpenCL information" << std::endl;
        bool error (false);
        logos::opencl_environment environment (error);
        if (!error)
        {
            environment.dump (std::cout);
            std::stringstream stream;
            environment.dump (stream);
            BOOST_LOG (node.logging.log) << stream.str ();
        }
        else
        {
            std::cout << "Error initializing OpenCL" << std::endl;
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
    else if (vm.count ("wallet_add_adhoc"))
    {
        if (vm.count ("wallet") == 1 && vm.count ("key") == 1)
        {
            logos::uint256_union wallet_id;
            if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
            {
                std::string password;
                if (vm.count ("password") > 0)
                {
                    password = vm["password"].as<std::string> ();
                }
                inactive_node node (data_path);
                auto wallet (node.node->wallets.open (wallet_id));
                if (wallet != nullptr)
                {
                    if (!wallet->enter_password (password))
                    {
                        logos::raw_key key;
                        if (!key.data.decode_hex (vm["key"].as<std::string> ()))
                        {
                            logos::transaction transaction (wallet->store.environment, nullptr, true);
                            wallet->store.insert_adhoc (transaction, key);
                        }
                        else
                        {
                            std::cerr << "Invalid key\n";
                            result = true;
                        }
                    }
                    else
                    {
                        std::cerr << "Invalid password\n";
                        result = true;
                    }
                }
                else
                {
                    std::cerr << "Wallet doesn't exist\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "Invalid wallet id\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option\n";
            result = true;
        }
    }
    else if (vm.count ("wallet_change_seed"))
    {
        if (vm.count ("wallet") == 1 && vm.count ("key") == 1)
        {
            logos::uint256_union wallet_id;
            if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
            {
                std::string password;
                if (vm.count ("password") > 0)
                {
                    password = vm["password"].as<std::string> ();
                }
                inactive_node node (data_path);
                auto wallet (node.node->wallets.open (wallet_id));
                if (wallet != nullptr)
                {
                    if (!wallet->enter_password (password))
                    {
                        logos::raw_key key;
                        if (!key.data.decode_hex (vm["key"].as<std::string> ()))
                        {
                            logos::transaction transaction (wallet->store.environment, nullptr, true);
                            wallet->change_seed (transaction, key);
                        }
                        else
                        {
                            std::cerr << "Invalid key\n";
                            result = true;
                        }
                    }
                    else
                    {
                        std::cerr << "Invalid password\n";
                        result = true;
                    }
                }
                else
                {
                    std::cerr << "Wallet doesn't exist\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "Invalid wallet id\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option\n";
            result = true;
        }
    }
    else if (vm.count ("wallet_create"))
    {
        inactive_node node (data_path);
        logos::keypair key;
        std::cout << key.pub.to_string () << std::endl;
        auto wallet (node.node->wallets.create (key.pub));
        wallet->enter_initial_password ();
    }
    else if (vm.count ("wallet_decrypt_unsafe"))
    {
        if (vm.count ("wallet") == 1)
        {
            std::string password;
            if (vm.count ("password") == 1)
            {
                password = vm["password"].as<std::string> ();
            }
            logos::uint256_union wallet_id;
            if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
            {
                inactive_node node (data_path);
                auto existing (node.node->wallets.items.find (wallet_id));
                if (existing != node.node->wallets.items.end ())
                {
                    if (!existing->second->enter_password (password))
                    {
                        logos::transaction transaction (existing->second->store.environment, nullptr, false);
                        logos::raw_key seed;
                        existing->second->store.seed (seed, transaction);
                        std::cout << boost::str (boost::format ("Seed: %1%\n") % seed.data.to_string ());
                        for (auto i (existing->second->store.begin (transaction)), m (existing->second->store.end ()); i != m; ++i)
                        {
                            logos::account account (i->first.uint256 ());
                            logos::raw_key key;
                            auto error (existing->second->store.fetch (transaction, account, key));
                            assert (!error);
                            std::cout << boost::str (boost::format ("Pub: %1% Prv: %2%\n") % account.to_account () % key.data.to_string ());
                        }
                    }
                    else
                    {
                        std::cerr << "Invalid password\n";
                        result = true;
                    }
                }
                else
                {
                    std::cerr << "Wallet doesn't exist\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "Invalid wallet id\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_decrypt_unsafe requires one <wallet> option\n";
            result = true;
        }
    }
    else if (vm.count ("wallet_destroy"))
    {
        if (vm.count ("wallet") == 1)
        {
            logos::uint256_union wallet_id;
            if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
            {
                inactive_node node (data_path);
                if (node.node->wallets.items.find (wallet_id) != node.node->wallets.items.end ())
                {
                    node.node->wallets.destroy (wallet_id);
                }
                else
                {
                    std::cerr << "Wallet doesn't exist\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "Invalid wallet id\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_destroy requires one <wallet> option\n";
            result = true;
        }
    }
    else if (vm.count ("wallet_import"))
    {
        if (vm.count ("file") == 1)
        {
            std::string filename (vm["file"].as<std::string> ());
            std::ifstream stream;
            stream.open (filename.c_str ());
            if (!stream.fail ())
            {
                std::stringstream contents;
                contents << stream.rdbuf ();
                std::string password;
                if (vm.count ("password") == 1)
                {
                    password = vm["password"].as<std::string> ();
                }
                if (vm.count ("wallet") == 1)
                {
                    logos::uint256_union wallet_id;
                    if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
                    {
                        inactive_node node (data_path);
                        auto existing (node.node->wallets.items.find (wallet_id));
                        if (existing != node.node->wallets.items.end ())
                        {
                            if (!existing->second->import (contents.str (), password))
                            {
                                result = false;
                            }
                            else
                            {
                                std::cerr << "Unable to import wallet\n";
                                result = true;
                            }
                        }
                        else
                        {
                            std::cerr << "Wallet doesn't exist\n";
                            result = true;
                        }
                    }
                    else
                    {
                        std::cerr << "Invalid wallet id\n";
                        result = true;
                    }
                }
                else
                {
                    std::cerr << "wallet_import requires one <wallet> option\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "Unable to open <file>\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_import requires one <file> option\n";
            result = true;
        }
    }
    else if (vm.count ("wallet_list"))
    {
        inactive_node node (data_path);
        for (auto i (node.node->wallets.items.begin ()), n (node.node->wallets.items.end ()); i != n; ++i)
        {
            std::cout << boost::str (boost::format ("Wallet ID: %1%\n") % i->first.to_string ());
            logos::transaction transaction (i->second->store.environment, nullptr, false);
            for (auto j (i->second->store.begin (transaction)), m (i->second->store.end ()); j != m; ++j)
            {
                std::cout << logos::uint256_union (j->first.uint256 ()).to_account () << '\n';
            }
        }
    }
    else if (vm.count ("wallet_remove"))
    {
        if (vm.count ("wallet") == 1 && vm.count ("account") == 1)
        {
            inactive_node node (data_path);
            logos::uint256_union wallet_id;
            if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
            {
                auto wallet (node.node->wallets.items.find (wallet_id));
                if (wallet != node.node->wallets.items.end ())
                {
                    logos::account account_id;
                    if (!account_id.decode_account (vm["account"].as<std::string> ()))
                    {
                        logos::transaction transaction (wallet->second->store.environment, nullptr, true);
                        auto account (wallet->second->store.find (transaction, account_id));
                        if (account != wallet->second->store.end ())
                        {
                            wallet->second->store.erase (transaction, account_id);
                        }
                        else
                        {
                            std::cerr << "Account not found in wallet\n";
                            result = true;
                        }
                    }
                    else
                    {
                        std::cerr << "Invalid account id\n";
                        result = true;
                    }
                }
                else
                {
                    std::cerr << "Wallet not found\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "Invalid wallet id\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_remove command requires one <wallet> and one <account> option\n";
            result = true;
        }
    }
    else if (vm.count ("wallet_representative_get"))
    {
        if (vm.count ("wallet") == 1)
        {
            logos::uint256_union wallet_id;
            if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
            {
                inactive_node node (data_path);
                auto wallet (node.node->wallets.items.find (wallet_id));
                if (wallet != node.node->wallets.items.end ())
                {
                    logos::transaction transaction (wallet->second->store.environment, nullptr, false);
                    auto representative (wallet->second->store.representative (transaction));
                    std::cout << boost::str (boost::format ("Representative: %1%\n") % representative.to_account ());
                }
                else
                {
                    std::cerr << "Wallet not found\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "Invalid wallet id\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_representative_get requires one <wallet> option\n";
            result = true;
        }
    }
    else if (vm.count ("wallet_representative_set"))
    {
        if (vm.count ("wallet") == 1)
        {
            if (vm.count ("account") == 1)
            {
                logos::uint256_union wallet_id;
                if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
                {
                    logos::account account;
                    if (!account.decode_account (vm["account"].as<std::string> ()))
                    {
                        inactive_node node (data_path);
                        auto wallet (node.node->wallets.items.find (wallet_id));
                        if (wallet != node.node->wallets.items.end ())
                        {
                            logos::transaction transaction (wallet->second->store.environment, nullptr, true);
                            wallet->second->store.representative_set (transaction, account);
                        }
                        else
                        {
                            std::cerr << "Wallet not found\n";
                            result = true;
                        }
                    }
                    else
                    {
                        std::cerr << "Invalid account\n";
                        result = true;
                    }
                }
                else
                {
                    std::cerr << "Invalid wallet id\n";
                    result = true;
                }
            }
            else
            {
                std::cerr << "wallet_representative_set requires one <account> option\n";
                result = true;
            }
        }
        else
        {
            std::cerr << "wallet_representative_set requires one <wallet> option\n";
            result = true;
        }
    }
    else if (vm.count ("vote_dump") == 1)
    {
        inactive_node node (data_path);
        logos::transaction transaction (node.node->store.environment, nullptr, false);
        for (auto i (node.node->store.vote_begin (transaction)), n (node.node->store.vote_end ()); i != n; ++i)
        {
            bool error (false);
            logos::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
            auto vote (std::make_shared<logos::vote> (error, stream));
            assert (!error);
            std::cerr << boost::str (boost::format ("%1%\n") % vote->to_json ());
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
alarm (*service),
work (1, nullptr)
{
    boost::filesystem::create_directories (path);
    logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
    logging.init (path);
    node = std::make_shared<logos::node> (init, *service, 24000, path, alarm, logging, work);
}

logos::inactive_node::~inactive_node ()
{
    node->stop ();
}

logos::port_mapping::port_mapping (logos::node & node_a) :
node (node_a),
devices (nullptr),
protocols ({ { { "TCP", 0, boost::asio::ip::address_v4::any (), 0 }, { "UDP", 0, boost::asio::ip::address_v4::any (), 0 } } }),
check_count (0),
on (false)
{
    urls = { 0 };
    data = { { 0 } };
}

void logos::port_mapping::start ()
{
    check_mapping_loop ();
}

void logos::port_mapping::refresh_devices ()
{
    if (logos::logos_network != logos::logos_networks::logos_test_network)
    {
        std::lock_guard<std::mutex> lock (mutex);
        int discover_error = 0;
        freeUPNPDevlist (devices);
        devices = upnpDiscover (2000, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, false, 2, &discover_error);
        std::array<char, 64> local_address;
        local_address.fill (0);
        auto igd_error (UPNP_GetValidIGD (devices, &urls, &data, local_address.data (), sizeof (local_address)));
        if (igd_error == 1 || igd_error == 2)
        {
            boost::system::error_code ec;
            address = boost::asio::ip::address_v4::from_string (local_address.data (), ec);
        }
        if (check_count % 15 == 0)
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("UPnP local address: %1%, discovery: %2%, IGD search: %3%") % local_address.data () % discover_error % igd_error);
            for (auto i (devices); i != nullptr; i = i->pNext)
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("UPnP device url: %1% st: %2% usn: %3%") % i->descURL % i->st % i->usn);
            }
        }
    }
}

void logos::port_mapping::refresh_mapping ()
{
    if (logos::logos_network != logos::logos_networks::logos_test_network)
    {
        std::lock_guard<std::mutex> lock (mutex);
        auto node_port (std::to_string (node.network.endpoint ().port ()));

        // Intentionally omitted: we don't map the RPC port because, unless RPC authentication was added, this would almost always be a security risk
        for (auto & protocol : protocols)
        {
            std::array<char, 6> actual_external_port;
            actual_external_port.fill (0);
            auto add_port_mapping_error (UPNP_AddAnyPortMapping (urls.controlURL, data.first.servicetype, node_port.c_str (), node_port.c_str (), address.to_string ().c_str (), nullptr, protocol.name, nullptr, std::to_string (mapping_timeout).c_str (), actual_external_port.data ()));
            if (check_count % 15 == 0)
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("UPnP %1% port mapping response: %2%, actual external port %3%") % protocol.name % add_port_mapping_error % actual_external_port.data ());
            }
            if (add_port_mapping_error == UPNPCOMMAND_SUCCESS)
            {
                protocol.external_port = std::atoi (actual_external_port.data ());
            }
            else
            {
                protocol.external_port = 0;
            }
        }
    }
}

int logos::port_mapping::check_mapping ()
{
    int result (3600);
    if (logos::logos_network != logos::logos_networks::logos_test_network)
    {
        // Long discovery time and fast setup/teardown make this impractical for testing
        std::lock_guard<std::mutex> lock (mutex);
        auto node_port (std::to_string (node.network.endpoint ().port ()));
        for (auto & protocol : protocols)
        {
            std::array<char, 64> int_client;
            std::array<char, 6> int_port;
            std::array<char, 16> remaining_mapping_duration;
            remaining_mapping_duration.fill (0);
            auto verify_port_mapping_error (UPNP_GetSpecificPortMappingEntry (urls.controlURL, data.first.servicetype, node_port.c_str (), protocol.name, nullptr, int_client.data (), int_port.data (), nullptr, nullptr, remaining_mapping_duration.data ()));
            if (verify_port_mapping_error == UPNPCOMMAND_SUCCESS)
            {
                protocol.remaining = result;
            }
            else
            {
                protocol.remaining = 0;
            }
            result = std::min (result, protocol.remaining);
            std::array<char, 64> external_address;
            external_address.fill (0);
            auto external_ip_error (UPNP_GetExternalIPAddress (urls.controlURL, data.first.servicetype, external_address.data ()));
            if (external_ip_error == UPNPCOMMAND_SUCCESS)
            {
                boost::system::error_code ec;
                protocol.external_address = boost::asio::ip::address_v4::from_string (external_address.data (), ec);
            }
            else
            {
                protocol.external_address = boost::asio::ip::address_v4::any ();
            }
            if (check_count % 15 == 0)
            {
                BOOST_LOG (node.log) << boost::str (boost::format ("UPnP %1% mapping verification response: %2%, external ip response: %3%, external ip: %4%, internal ip: %5%, remaining lease: %6%") % protocol.name % verify_port_mapping_error % external_ip_error % external_address.data () % address.to_string () % remaining_mapping_duration.data ());
            }
        }
    }
    return result;
}

void logos::port_mapping::check_mapping_loop ()
{
    int wait_duration = check_timeout;
    refresh_devices ();
    if (devices != nullptr)
    {
        auto remaining (check_mapping ());
        // If the mapping is lost, refresh it
        if (remaining == 0)
        {
            refresh_mapping ();
        }
    }
    else
    {
        wait_duration = 300;
        if (check_count < 10)
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("UPnP No IGD devices found"));
        }
    }
    ++check_count;
    if (on)
    {
        auto node_l (node.shared ());
        node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (wait_duration), [node_l]() {
            node_l->port_mapping.check_mapping_loop ();
        });
    }
}

void logos::port_mapping::stop ()
{
    on = false;
    std::lock_guard<std::mutex> lock (mutex);
    for (auto & protocol : protocols)
    {
        if (protocol.external_port != 0)
        {
            // Be a good citizen for the router and shut down our mapping
            auto delete_error (UPNP_DeletePortMapping (urls.controlURL, data.first.servicetype, std::to_string (protocol.external_port).c_str (), protocol.name, address.to_string ().c_str ()));
            BOOST_LOG (node.log) << boost::str (boost::format ("Shutdown port mapping response: %1%") % delete_error);
        }
    }
    freeUPNPDevlist (devices);
    devices = nullptr;
}
