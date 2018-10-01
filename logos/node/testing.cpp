#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cstdlib>
#include <logos/node/common.hpp>
#include <logos/node/testing.hpp>

logos::system::system (uint16_t port_a, boost::filesystem::path &data_path) :
alarm (service),
work (1, nullptr)
{
    int count_a = 1;
    logging.init (data_path);
    nodes.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        std::cout << "logos::system::system init <1>..." << std::endl;
        logos::node_init init;
        logos::node_config config (port_a + i, logging);
        std::cout << " line: " << __LINE__ << " file: " << __FILE__ << std::endl;
        auto node (std::make_shared<logos::node> (init, service, data_path, alarm, config, work)); // RGD Replaced unique_path with test provided data_path, see daemon.cpp grep logos::node...
        std::cout << " line: " << __LINE__ << " file: " << __FILE__ << std::endl;
        assert (!init.error ());
        node->start ();
        std::cout << " line: " << __LINE__ << " file: " << __FILE__ << std::endl;
        logos::uint256_union wallet;
        logos::random_pool.GenerateBlock (wallet.bytes.data (), wallet.bytes.size ());
        std::cout << " line: " << __LINE__ << " file: " << __FILE__ << std::endl;
        node->wallets.create (wallet);
        nodes.push_back (node);
        std::cout << "logos::system::system init done <1>..." << std::endl;
    }
    for (auto i (nodes.begin ()), j (nodes.begin () + 1), n (nodes.end ()); j != n; ++i, ++j)
    {
        std::cout << "logos::system::system init <2>..." << std::endl;
        auto starting1 ((*i)->peers.size ());
        auto new1 (starting1);
        auto starting2 ((*j)->peers.size ());
        auto new2 (starting2);
        (*j)->network.send_keepalive ((*i)->network.endpoint ());
        do
        {
            poll ();
            new1 = (*i)->peers.size ();
            new2 = (*j)->peers.size ();
        } while (new1 == starting1 || new2 == starting2);
        std::cout << "logos::system::system init done <2>..." << std::endl;
    }
    auto iterations1 (0);
    while (std::any_of (nodes.begin (), nodes.end (), [](std::shared_ptr<logos::node> const & node_a) { return node_a->bootstrap_initiator.in_progress (); }))
    {
        std::cout << "logos::system::system init <3>..." << std::endl;
        poll ();
        ++iterations1;
        assert (iterations1 < 10000);
        std::cout << "logos::system::system init done <3>..." << std::endl;
    }
}

logos::system::system (uint16_t port_a, size_t count_a) :
alarm (service),
work (1, nullptr)
{
    logging.init (logos::unique_path ());
    nodes.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        logos::node_init init;
        logos::node_config config (port_a + i, logging);
        auto node (std::make_shared<logos::node> (init, service, logos::unique_path (), alarm, config, work));
        assert (!init.error ());
        node->start ();
        logos::uint256_union wallet;
        logos::random_pool.GenerateBlock (wallet.bytes.data (), wallet.bytes.size ());
        node->wallets.create (wallet);
        nodes.push_back (node);
    }
    for (auto i (nodes.begin ()), j (nodes.begin () + 1), n (nodes.end ()); j != n; ++i, ++j)
    {
        auto starting1 ((*i)->peers.size ());
        auto new1 (starting1);
        auto starting2 ((*j)->peers.size ());
        auto new2 (starting2);
        (*j)->network.send_keepalive ((*i)->network.endpoint ());
        do
        {
            poll ();
            new1 = (*i)->peers.size ();
            new2 = (*j)->peers.size ();
        } while (new1 == starting1 || new2 == starting2);
    }
    auto iterations1 (0);
    while (std::any_of (nodes.begin (), nodes.end (), [](std::shared_ptr<logos::node> const & node_a) { return node_a->bootstrap_initiator.in_progress (); }))
    {
        poll ();
        ++iterations1;
        assert (iterations1 < 10000);
    }
}

logos::system::~system ()
{
    for (auto & i : nodes)
    {
        i->stop ();
    }

    // Clean up tmp directories created by the tests. Since it's sometimes useful to
    // see log files after test failures, an environment variable is supported to
    // retain the files.
    if (std::getenv ("TEST_KEEP_TMPDIRS") == nullptr)
    {
        logos::remove_temporary_directories ();
    }
}

std::shared_ptr<logos::wallet> logos::system::wallet (size_t index_a)
{
    assert (nodes.size () > index_a);
    auto size (nodes[index_a]->wallets.items.size ());
    assert (size >= 1);
    return nodes[index_a]->wallets.items.begin ()->second;
}

logos::account logos::system::account (MDB_txn * transaction_a, size_t index_a)
{
    auto wallet_l (wallet (index_a));
    auto keys (wallet_l->store.begin (transaction_a));
    assert (keys != wallet_l->store.end ());
    auto result (keys->first);
    assert (++keys == wallet_l->store.end ());
    return result.uint256 ();
}

void logos::system::poll ()
{
    auto polled1 (service.poll_one ());
    if (polled1 == 0)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
}

namespace
{
class traffic_generator : public std::enable_shared_from_this<traffic_generator>
{
public:
    traffic_generator (uint32_t count_a, uint32_t wait_a, std::shared_ptr<logos::node> node_a, logos::system & system_a) :
    count (count_a),
    wait (wait_a),
    node (node_a),
    system (system_a)
    {
    }
    void run ()
    {
        auto count_l (count - 1);
        count = count_l - 1;
        system.generate_activity (*node, accounts);
        if (count_l > 0)
        {
            auto this_l (shared_from_this ());
            node->alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (wait), [this_l]() { this_l->run (); });
        }
    }
    std::vector<logos::account> accounts;
    uint32_t count;
    uint32_t wait;
    std::shared_ptr<logos::node> node;
    logos::system & system;
};
}

void logos::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a)
{
    for (size_t i (0), n (nodes.size ()); i != n; ++i)
    {
        generate_usage_traffic (count_a, wait_a, i);
    }
}

void logos::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a, size_t index_a)
{
    assert (nodes.size () > index_a);
    assert (count_a > 0);
    auto generate (std::make_shared<traffic_generator> (count_a, wait_a, nodes[index_a], *this));
    generate->run ();
}

void logos::system::generate_rollback (logos::node & node_a, std::vector<logos::account> & accounts_a)
{
    logos::transaction transaction (node_a.store.environment, nullptr, true);
    auto index (random_pool.GenerateWord32 (0, accounts_a.size () - 1));
    auto account (accounts_a[index]);
    logos::account_info info;
    auto error (node_a.store.account_get (transaction, account, info));
    if (!error)
    {
        auto hash (info.open_block);
        logos::genesis genesis;
        if (hash != genesis.hash ())
        {
            accounts_a[index] = accounts_a[accounts_a.size () - 1];
            accounts_a.pop_back ();
            //node_a.ledger.rollback (transaction, hash);
        }
    }
}

void logos::system::generate_receive (logos::node & node_a)
{
/*    std::shared_ptr<logos::block> send_block;
    {
        logos::transaction transaction (node_a.store.environment, nullptr, false);
        logos::uint256_union random_block;
        random_pool.GenerateBlock (random_block.bytes.data (), sizeof (random_block.bytes));
        auto i (node_a.store.pending_begin (transaction, logos::pending_key (random_block, 0)));
        if (i != node_a.store.pending_end ())
        {
            logos::pending_key send_hash (i->first);
            logos::pending_info info (i->second);
            send_block = node_a.store.block_get (transaction, send_hash.hash);
        }
    }
    if (send_block != nullptr)
    {
        auto receive_error (wallet (0)->receive_sync (send_block, logos::genesis_account, std::numeric_limits<logos::uint128_t>::max ()));
        (void)receive_error;
    }*/
}

void logos::system::generate_activity (logos::node & node_a, std::vector<logos::account> & accounts_a)
{
    auto what (random_pool.GenerateByte ());
    if (what < 0x1)
    {
        generate_rollback (node_a, accounts_a);
    }
    else if (what < 0x10)
    {
        generate_change_known (node_a, accounts_a);
    }
    else if (what < 0x20)
    {
        generate_change_unknown (node_a, accounts_a);
    }
    else if (what < 0x70)
    {
        generate_receive (node_a);
    }
    else if (what < 0xc0)
    {
        generate_send_existing (node_a, accounts_a);
    }
    else
    {
        generate_send_new (node_a, accounts_a);
    }
}

logos::account logos::system::get_random_account (std::vector<logos::account> & accounts_a)
{
    auto index (random_pool.GenerateWord32 (0, accounts_a.size () - 1));
    auto result (accounts_a[index]);
    return result;
}

logos::uint128_t logos::system::get_random_amount (MDB_txn * transaction_a, logos::node & node_a, logos::account const & account_a)
{
    logos::uint128_t balance (node_a.ledger.account_balance (transaction_a, account_a));
    std::string balance_text (balance.convert_to<std::string> ());
    logos::uint128_union random_amount;
    random_pool.GenerateBlock (random_amount.bytes.data (), sizeof (random_amount.bytes));
    auto result (((logos::uint256_t{ random_amount.number () } * balance) / logos::uint256_t{ std::numeric_limits<logos::uint128_t>::max () }).convert_to<logos::uint128_t> ());
    std::string text (result.convert_to<std::string> ());
    return result;
}

void logos::system::generate_send_existing (logos::node & node_a, std::vector<logos::account> & accounts_a)
{
    logos::uint128_t amount;
    logos::account destination;
    logos::account source;
    {
        logos::account account;
        random_pool.GenerateBlock (account.bytes.data (), sizeof (account.bytes));
        logos::transaction transaction (node_a.store.environment, nullptr, false);
        logos::store_iterator entry (node_a.store.latest_begin (transaction, account));
        if (entry == node_a.store.latest_end ())
        {
            entry = node_a.store.latest_begin (transaction);
        }
        assert (entry != node_a.store.latest_end ());
        destination = logos::account (entry->first.uint256 ());
        source = get_random_account (accounts_a);
        amount = get_random_amount (transaction, node_a, source);
    }
    if (!amount.is_zero ())
    {
        auto hash (wallet (0)->send_sync (source, destination, amount));
        assert (!hash.is_zero ());
    }
}

void logos::system::generate_change_known (logos::node & node_a, std::vector<logos::account> & accounts_a)
{
    logos::account source (get_random_account (accounts_a));
    if (!node_a.latest (source).is_zero ())
    {
        logos::account destination (get_random_account (accounts_a));
        auto change_error (wallet (0)->change_sync (source, destination));
        assert (!change_error);
    }
}

void logos::system::generate_change_unknown (logos::node & node_a, std::vector<logos::account> & accounts_a)
{
    logos::account source (get_random_account (accounts_a));
    if (!node_a.latest (source).is_zero ())
    {
        logos::keypair key;
        logos::account destination (key.pub);
        auto change_error (wallet (0)->change_sync (source, destination));
        assert (!change_error);
    }
}

void logos::system::generate_send_new (logos::node & node_a, std::vector<logos::account> & accounts_a)
{
    assert (node_a.wallets.items.size () == 1);
    logos::uint128_t amount;
    logos::account source;
    {
        logos::transaction transaction (node_a.store.environment, nullptr, false);
        source = get_random_account (accounts_a);
        amount = get_random_amount (transaction, node_a, source);
    }
    if (!amount.is_zero ())
    {
        auto pub (node_a.wallets.items.begin ()->second->deterministic_insert ());
        accounts_a.push_back (pub);
        auto hash (wallet (0)->send_sync (source, pub, amount));
        assert (!hash.is_zero ());
    }
}

void logos::system::generate_mass_activity (uint32_t count_a, logos::node & node_a)
{
    std::vector<logos::account> accounts;
    wallet (0)->insert_adhoc (logos::test_genesis_key.prv);
    accounts.push_back (logos::test_genesis_key.pub);
    auto previous (std::chrono::steady_clock::now ());
    for (uint32_t i (0); i < count_a; ++i)
    {
        if ((i & 0xff) == 0)
        {
            auto now (std::chrono::steady_clock::now ());
            auto us (std::chrono::duration_cast<std::chrono::microseconds> (now - previous).count ());
            uint64_t count (0);
            uint64_t state (0);
            {
                logos::transaction transaction (node_a.store.environment, nullptr, false);
                auto block_counts (node_a.store.block_count (transaction));
                count = block_counts.sum ();
                state = block_counts.state;
            }
            std::cerr << boost::str (boost::format ("Mass activity iteration %1% us %2% us/t %3% state: %4% old: %5%\n") % i % us % (us / 256) % state % (count - state));
            previous = now;
        }
        generate_activity (node_a, accounts);
    }
}

void logos::system::stop ()
{
    for (auto i : nodes)
    {
        i->stop ();
    }
    work.stop ();
}

logos::landing_store::landing_store ()
{
}

logos::landing_store::landing_store (logos::account const & source_a, logos::account const & destination_a, uint64_t start_a, uint64_t last_a) :
source (source_a),
destination (destination_a),
start (start_a),
last (last_a)
{
}

logos::landing_store::landing_store (bool & error_a, std::istream & stream_a)
{
    error_a = deserialize (stream_a);
}

bool logos::landing_store::deserialize (std::istream & stream_a)
{
    bool result;
    try
    {
        boost::property_tree::ptree tree;
        boost::property_tree::read_json (stream_a, tree);
        auto source_l (tree.get<std::string> ("source"));
        auto destination_l (tree.get<std::string> ("destination"));
        auto start_l (tree.get<std::string> ("start"));
        auto last_l (tree.get<std::string> ("last"));
        result = source.decode_account (source_l);
        if (!result)
        {
            result = destination.decode_account (destination_l);
            if (!result)
            {
                start = std::stoull (start_l);
                last = std::stoull (last_l);
            }
        }
    }
    catch (std::logic_error const &)
    {
        result = true;
    }
    catch (std::runtime_error const &)
    {
        result = true;
    }
    return result;
}

void logos::landing_store::serialize (std::ostream & stream_a) const
{
    boost::property_tree::ptree tree;
    tree.put ("source", source.to_account ());
    tree.put ("destination", destination.to_account ());
    tree.put ("start", std::to_string (start));
    tree.put ("last", std::to_string (last));
    boost::property_tree::write_json (stream_a, tree);
}

bool logos::landing_store::operator== (logos::landing_store const & other_a) const
{
    return source == other_a.source && destination == other_a.destination && start == other_a.start && last == other_a.last;
}

logos::landing::landing (logos::node & node_a, std::shared_ptr<logos::wallet> wallet_a, logos::landing_store & store_a, boost::filesystem::path const & path_a) :
path (path_a),
store (store_a),
wallet (wallet_a),
node (node_a)
{
}

void logos::landing::write_store ()
{
    std::ofstream store_file;
    store_file.open (path.string ());
    if (!store_file.fail ())
    {
        store.serialize (store_file);
    }
    else
    {
        std::stringstream str;
        store.serialize (str);
        BOOST_LOG (node.log) << boost::str (boost::format ("Error writing store file %1%") % str.str ());
    }
}

logos::uint128_t logos::landing::distribution_amount (uint64_t interval)
{
    // Halving period ~= Exponent of 2 in seconds approximately 1 year = 2^25 = 33554432
    // Interval = Exponent of 2 in seconds approximately 1 minute = 2^10 = 64
    uint64_t intervals_per_period (1 << (25 - interval_exponent));
    logos::uint128_t result;
    if (interval < intervals_per_period * 1)
    {
        // Total supply / 2^halving period / intervals per period
        // 2^128 / 2^1 / (2^25 / 2^10)
        result = logos::uint128_t (1) << (127 - (25 - interval_exponent)); // 50%
    }
    else if (interval < intervals_per_period * 2)
    {
        result = logos::uint128_t (1) << (126 - (25 - interval_exponent)); // 25%
    }
    else if (interval < intervals_per_period * 3)
    {
        result = logos::uint128_t (1) << (125 - (25 - interval_exponent)); // 13%
    }
    else if (interval < intervals_per_period * 4)
    {
        result = logos::uint128_t (1) << (124 - (25 - interval_exponent)); // 6.3%
    }
    else if (interval < intervals_per_period * 5)
    {
        result = logos::uint128_t (1) << (123 - (25 - interval_exponent)); // 3.1%
    }
    else if (interval < intervals_per_period * 6)
    {
        result = logos::uint128_t (1) << (122 - (25 - interval_exponent)); // 1.6%
    }
    else if (interval < intervals_per_period * 7)
    {
        result = logos::uint128_t (1) << (121 - (25 - interval_exponent)); // 0.8%
    }
    else if (interval < intervals_per_period * 8)
    {
        result = logos::uint128_t (1) << (121 - (25 - interval_exponent)); // 0.8*
    }
    else
    {
        result = 0;
    }
    return result;
}

void logos::landing::distribute_one ()
{
    auto now (logos::seconds_since_epoch ());
    logos::block_hash last (1);
    while (!last.is_zero () && store.last + distribution_interval.count () < now)
    {
        auto amount (distribution_amount ((store.last - store.start) >> interval_exponent));
        last = wallet->send_sync (store.source, store.destination, amount);
        if (!last.is_zero ())
        {
            BOOST_LOG (node.log) << boost::str (boost::format ("Successfully distributed %1% in block %2%") % amount % last.to_string ());
            store.last += distribution_interval.count ();
            write_store ();
        }
        else
        {
            BOOST_LOG (node.log) << "Error while sending distribution";
        }
    }
}

void logos::landing::distribute_ongoing ()
{
    distribute_one ();
    BOOST_LOG (node.log) << "Waiting for next distribution cycle";
    node.alarm.add (std::chrono::steady_clock::now () + sleep_seconds, [this]() { distribute_ongoing (); });
}

std::chrono::seconds constexpr logos::landing::distribution_interval;
std::chrono::seconds constexpr logos::landing::sleep_seconds;
