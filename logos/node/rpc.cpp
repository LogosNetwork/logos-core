#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <logos/node/rpc.hpp>
#include <logos/microblock/microblock_tester.hpp>

#include <logos/lib/interface.h>
#include <logos/node/node.hpp>

#include <logos/request/utility.hpp>

#include <ed25519-donna/ed25519.h>

#ifdef LOGOS_SECURE_RPC
#include <logos/node/rpc_secure.hpp>
#endif

logos::rpc_secure_config::rpc_secure_config () :
enable (false),
verbose_logging (false)
{
}

void logos::rpc_secure_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
    tree_a.put ("enable", enable);
    tree_a.put ("verbose_logging", verbose_logging);
    tree_a.put ("server_key_passphrase", server_key_passphrase);
    tree_a.put ("server_cert_path", server_cert_path);
    tree_a.put ("server_key_path", server_key_path);
    tree_a.put ("server_dh_path", server_dh_path);
    tree_a.put ("client_certs_path", client_certs_path);
}

bool logos::rpc_secure_config::deserialize_json (boost::property_tree::ptree const & tree_a)
{
    auto error (false);
    try
    {
        enable = tree_a.get<bool> ("enable");
        verbose_logging = tree_a.get<bool> ("verbose_logging");
        server_key_passphrase = tree_a.get<std::string> ("server_key_passphrase");
        server_cert_path = tree_a.get<std::string> ("server_cert_path");
        server_key_path = tree_a.get<std::string> ("server_key_path");
        server_dh_path = tree_a.get<std::string> ("server_dh_path");
        client_certs_path = tree_a.get<std::string> ("client_certs_path");
    }
    catch (std::runtime_error const &)
    {
        error = true;
    }
    return error;
}

logos::rpc_config::rpc_config () :
address (boost::asio::ip::address_v6::loopback ()),
port (logos::rpc::rpc_port),
enable_control (false),
frontier_request_limit (16384),
chain_request_limit (16384)
{
}

logos::rpc_config::rpc_config (bool enable_control_a) :
address (boost::asio::ip::address_v6::loopback ()),
port (logos::rpc::rpc_port),
enable_control (enable_control_a),
frontier_request_limit (16384),
chain_request_limit (16384)
{
}

void logos::rpc_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
    tree_a.put ("address", address.to_string ());
    tree_a.put ("port", std::to_string (port));
    tree_a.put ("enable_control", enable_control);
    tree_a.put ("frontier_request_limit", frontier_request_limit);
    tree_a.put ("chain_request_limit", chain_request_limit);
}

bool logos::rpc_config::deserialize_json (boost::property_tree::ptree const & tree_a)
{
    auto result (false);
    try
    {
        auto rpc_secure_l (tree_a.get_child_optional ("secure"));
        if (rpc_secure_l)
        {
            result = secure.deserialize_json (rpc_secure_l.get ());
        }

        if (!result)
        {
            auto address_l (tree_a.get<std::string> ("address"));
            auto port_l (tree_a.get<std::string> ("port"));
            enable_control = tree_a.get<bool> ("enable_control");
            auto frontier_request_limit_l (tree_a.get<std::string> ("frontier_request_limit"));
            auto chain_request_limit_l (tree_a.get<std::string> ("chain_request_limit"));
            try
            {
                port = std::stoul (port_l);
                result = port > std::numeric_limits<uint16_t>::max ();
                frontier_request_limit = std::stoull (frontier_request_limit_l);
                chain_request_limit = std::stoull (chain_request_limit_l);
            }
            catch (std::logic_error const &)
            {
                result = true;
            }
            boost::system::error_code ec;
            address = boost::asio::ip::address_v6::from_string (address_l, ec);
            if (ec)
            {
                result = true;
            }
        }
    }
    catch (std::runtime_error const &)
    {
        result = true;
    }
    return result;
}

logos::rpc::rpc (boost::asio::io_service & service_a, logos::node & node_a, logos::rpc_config const & config_a) :
acceptor (service_a),
config (config_a),
node (node_a)
{
}

void logos::rpc::start ()
{
    auto endpoint (logos::tcp_endpoint (config.address, config.port));
    acceptor.open (endpoint.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

    boost::system::error_code ec;
    acceptor.bind (endpoint, ec);
    if (ec)
    {
        LOG_ERROR (node.log) << boost::str (boost::format ("Error while binding for RPC on port %1%: %2%") % endpoint.port () % ec.message ());
        throw std::runtime_error (ec.message ());
    }

    acceptor.listen ();
    accept ();
}

void logos::rpc::accept ()
{
    auto connection (std::make_shared<logos::rpc_connection> (node, *this));
    acceptor.async_accept (connection->socket, [this, connection](boost::system::error_code const & ec) {
        if (!ec)
        {
            accept ();
            connection->parse_connection ();
        }
        else
        {
            LOG_ERROR (this->node.log) << boost::str (boost::format ("Error accepting RPC connections: %1%") % ec);
        }
    });
}

void logos::rpc::stop ()
{
    acceptor.close ();
}

logos::rpc_handler::rpc_handler (logos::node & node_a, logos::rpc & rpc_a, std::string const & body_a, std::function<void(boost::property_tree::ptree const &)> const & response_a) :
body (body_a),
node (node_a),
rpc (rpc_a),
response (response_a)
{
}

void logos::rpc::observer_action (logos::account const & account_a)
{
    std::shared_ptr<logos::payment_observer> observer;
    {
        std::lock_guard<std::mutex> lock (mutex);
        auto existing (payment_observers.find (account_a));
        if (existing != payment_observers.end ())
        {
            observer = existing->second;
        }
    }
    if (observer != nullptr)
    {
        observer->observe ();
    }
}

void logos::error_response_ (std::function<void(boost::property_tree::ptree const &)> response_a, std::string const & message_a)
{
    boost::property_tree::ptree response_l;
    response_l.put ("error", message_a);
    response_a (response_l);
}

#define error_response(r,m) {error_response_(r,m);return;}

namespace
{
bool decode_unsigned (std::string const & text, uint64_t & number)
{
    bool result;
    size_t end;
    try
    {
        number = std::stoull (text, &end);
        result = false;
    }
    catch (std::invalid_argument const &)
    {
        result = true;
    }
    catch (std::out_of_range const &)
    {
        result = true;
    }
    result = result || end != text.size ();
    return result;
}
}

void logos::rpc_handler::account_balance ()
{
    auto res = account_balance(
            request,node.store);
    if(!res.error)
    {
        response(res.contents);
    }
    else
    {
        error_response(response,res.error_msg);
    }
}

void logos::rpc_handler::account_block_count ()
{
//    std::string account_text (request.get<std::string> ("account"));
//    logos::uint256_union account;
//    auto error (account.decode_account (account_text));
//    if (!error)
//    {
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        logos::account_info info;
//        if (!node.store.account_get (transaction, account, info))
//        {
//            boost::property_tree::ptree response_l;
//            response_l.put ("request_count", std::to_string (info.block_count + info.receive_count));
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Account not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad account number");
//    }
}

void logos::rpc_handler::account_create ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            const bool generate_work = request.get<bool> ("work", true);
//            logos::account new_key (existing->second->deterministic_insert (generate_work));
//            if (!new_key.is_zero ())
//            {
//                boost::property_tree::ptree response_l;
//                response_l.put ("account", new_key.to_account ());
//                response (response_l);
//            }
//            else
//            {
//                error_response (response, "Wallet is locked");
//            }
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::account_from_key ()
{
    std::string key_text (request.get<std::string> ("key"));
    logos::uint256_union pub;
    auto error (pub.decode_hex (key_text));
    if (!error)
    {
        boost::property_tree::ptree response_l;
        response_l.put ("account", pub.to_account ());
        response (response_l);
    }
    else
    {
        error_response (response, "Bad public key");
    }
}

void logos::rpc_handler::account_info ()
{
    auto res = account_info(
            request,node.store);
    if(!res.error)
    {
        response(res.contents);
    }
    else
    {
        error_response(response,res.error_msg);
    }
}

void logos::rpc_handler::account_to_key ()
{
    std::string account_text (request.get<std::string> ("account"));
    logos::account account;
    auto error (account.decode_account (account_text));
    if (!error)
    {
        boost::property_tree::ptree response_l;
        response_l.put ("key", account.to_string ());
        response (response_l);
    }
    else
    {
        error_response (response, "Bad account number");
    }
}

void logos::rpc_handler::account_list ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            boost::property_tree::ptree response_l;
//            boost::property_tree::ptree accounts;
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            for (auto i (existing->second->store.begin (transaction)), j (existing->second->store.end ()); i != j; ++i)
//            {
//                boost::property_tree::ptree entry;
//                entry.put ("", logos::uint256_union (i->first.uint256 ()).to_account ());
//                accounts.push_back (std::make_pair ("", entry));
//            }
//            response_l.add_child ("accounts", accounts);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::account_move ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    std::string source_text (request.get<std::string> ("source"));
//    auto accounts_text (request.get_child ("accounts"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            auto wallet (existing->second);
//            logos::uint256_union source;
//            auto error (source.decode_hex (source_text));
//            if (!error)
//            {
//                auto existing (node.wallets.items.find (source));
//                if (existing != node.wallets.items.end ())
//                {
//                    auto source (existing->second);
//                    std::vector<logos::public_key> accounts;
//                    for (auto i (accounts_text.begin ()), n (accounts_text.end ()); i != n; ++i)
//                    {
//                        logos::public_key account;
//                        account.decode_hex (i->second.get<std::string> (""));
//                        accounts.push_back (account);
//                    }
//                    logos::transaction transaction (node.store.environment, nullptr, true);
//                    auto error (wallet->store.move (transaction, source->store, accounts));
//                    boost::property_tree::ptree response_l;
//                    response_l.put ("moved", error ? "0" : "1");
//                    response (response_l);
//                }
//                else
//                {
//                    error_response (response, "Source not found");
//                }
//            }
//            else
//            {
//                error_response (response, "Bad source number");
//            }
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::account_remove ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    std::string account_text (request.get<std::string> ("account"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            auto wallet (existing->second);
//            logos::transaction transaction (node.store.environment, nullptr, true);
//            if (existing->second->store.valid_password (transaction))
//            {
//                logos::account account_id;
//                auto error (account_id.decode_account (account_text));
//                if (!error)
//                {
//                    auto account (wallet->store.find (transaction, account_id));
//                    if (account != wallet->store.end ())
//                    {
//                        wallet->store.erase (transaction, account_id);
//                        boost::property_tree::ptree response_l;
//                        response_l.put ("removed", "1");
//                        response (response_l);
//                    }
//                    else
//                    {
//                        error_response (response, "Account not found in wallet");
//                    }
//                }
//                else
//                {
//                    error_response (response, "Bad account number");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet locked");
//            }
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::account_representative ()
{
//    std::string account_text (request.get<std::string> ("account"));
//    logos::account account;
//    auto error (account.decode_account (account_text));
//    if (!error)
//    {
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        logos::account_info info;
//        auto error (node.store.account_get (transaction, account, info));
//        if (!error)
//        {
//            auto block (node.store.block_get (transaction, info.governance_subchain_head));
//            assert (block != nullptr);
//            boost::property_tree::ptree response_l;
//            response_l.put ("representative", block->representative ().to_account ());
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Account not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad account number");
//    }
}

void logos::rpc_handler::account_representative_set ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                auto wallet (existing->second);
//                std::string account_text (request.get<std::string> ("account"));
//                logos::account account;
//                auto error (account.decode_account (account_text));
//                if (!error)
//                {
//                    std::string representative_text (request.get<std::string> ("representative"));
//                    logos::account representative;
//                    auto error (representative.decode_account (representative_text));
//                    if (!error)
//                    {
//                        uint64_t work (0);
//                        boost::optional<std::string> work_text (request.get_optional<std::string> ("work"));
//                        if (work_text.is_initialized ())
//                        {
//                            auto work_error (logos::from_string_hex (work_text.get (), work));
//                            if (work_error)
//                            {
//                                error_response (response, "Bad work");
//                            }
//                        }
//                        if (work)
//                        {
//                            logos::transaction transaction (node.store.environment, nullptr, true);
//                            logos::account_info info;
//                            if (!node.store.account_get (transaction, account, info))
//                            {
//                                if (!logos::work_validate (info.head, work))
//                                {
//                                    existing->second->store.work_put (transaction, account, work);
//                                }
//                                else
//                                {
//                                    error_response (response, "Invalid work");
//                                }
//                            }
//                            else
//                            {
//                                error_response (response, "Account not found");
//                            }
//                        }
//                        auto response_a (response);
//                        wallet->change_async (account, representative, [response_a](std::shared_ptr<logos::block> block) {
//                            logos::block_hash hash (0);
//                            if (block != nullptr)
//                            {
//                                hash = block->hash ();
//                            }
//                            boost::property_tree::ptree response_l;
//                            response_l.put ("block", hash.to_string ());
//                            response_a (response_l);
//                        },
//                        work == 0);
//                    }
//                }
//                else
//                {
//                    error_response (response, "Bad account number");
//                }
//            }
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::account_weight ()
{
    std::string account_text (request.get<std::string> ("account"));
    logos::uint256_union account;
    auto error (account.decode_account (account_text));
    if (!error)
    {
//        auto balance (node.weight (account));
//        boost::property_tree::ptree response_l;
//        response_l.put ("weight", balance.convert_to<std::string> ());
//        response (response_l);
    }
    else
    {
        error_response (response, "Bad account number");
    }
}

void logos::rpc_handler::accounts_balances ()
{
//    boost::property_tree::ptree response_l;
//    boost::property_tree::ptree balances;
//    for (auto & accounts : request.get_child ("accounts"))
//    {
//        std::string account_text = accounts.second.data ();
//        logos::uint256_union account;
//        auto error (account.decode_account (account_text));
//        if (!error)
//        {
//            boost::property_tree::ptree entry;
//            auto balance (node.balance_pending (account));
//            entry.put ("balance", balance.first.convert_to<std::string> ());
//            entry.put ("pending", balance.second.convert_to<std::string> ());
//            balances.push_back (std::make_pair (account.to_account (), entry));
//        }
//        else
//        {
//            error_response (response, "Bad account number");
//        }
//    }
//    response_l.add_child ("balances", balances);
//    response (response_l);
}

void logos::rpc_handler::accounts_create ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        uint64_t count;
//        std::string count_text (request.get<std::string> ("count"));
//        auto count_error (decode_unsigned (count_text, count));
//        if (!count_error && count != 0)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                const bool generate_work = request.get<bool> ("work", false);
//                boost::property_tree::ptree response_l;
//                boost::property_tree::ptree accounts;
//                for (auto i (0); accounts.size () < count; ++i)
//                {
//                    logos::account new_key (existing->second->deterministic_insert (generate_work));
//                    if (!new_key.is_zero ())
//                    {
//                        boost::property_tree::ptree entry;
//                        entry.put ("", new_key.to_account ());
//                        accounts.push_back (std::make_pair ("", entry));
//                    }
//                }
//                response_l.add_child ("accounts", accounts);
//                response (response_l);
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Invalid count limit");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::accounts_exist ()
{
    auto res = accounts_exist(request, node.store);
    if(!res.error)
    {
        response(res.contents);
    }
    else
    {
        error_response(response,res.error_msg);
    }
}

void logos::rpc_handler::accounts_frontiers ()
{
    boost::property_tree::ptree response_l;
    boost::property_tree::ptree frontiers;
    logos::transaction transaction (node.store.environment, nullptr, false);
    for (auto & accounts : request.get_child ("accounts"))
    {
        std::string account_text = accounts.second.data ();
        logos::uint256_union account;
        auto error (account.decode_account (account_text));
        if (!error)
        {
//            auto latest (node.ledger.latest (transaction, account));
//            if (!latest.is_zero ())
//            {
//                frontiers.put (account.to_account (), latest.to_string ());
//            }
        }
        else
        {
            error_response (response, "Bad account number");
        }
    }
    response_l.add_child ("frontiers", frontiers);
    response (response_l);
}

void logos::rpc_handler::accounts_pending ()
{
//    uint64_t count (std::numeric_limits<uint64_t>::max ());
//    logos::uint128_union threshold (0);
//    boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
//    if (count_text.is_initialized ())
//    {
//        auto error (decode_unsigned (count_text.get (), count));
//        if (error)
//        {
//            error_response (response, "Invalid count limit");
//        }
//    }
//    boost::optional<std::string> threshold_text (request.get_optional<std::string> ("threshold"));
//    if (threshold_text.is_initialized ())
//    {
//        auto error_threshold (threshold.decode_dec (threshold_text.get ()));
//        if (error_threshold)
//        {
//            error_response (response, "Bad threshold number");
//        }
//    }
//    const bool source = request.get<bool> ("source", false);
//    boost::property_tree::ptree response_l;
//    boost::property_tree::ptree pending;
//    logos::transaction transaction (node.store.environment, nullptr, false);
//    for (auto & accounts : request.get_child ("accounts"))
//    {
//        std::string account_text = accounts.second.data ();
//        logos::uint256_union account;
//        if (!account.decode_account (account_text))
//        {
//            boost::property_tree::ptree peers_l;
//            logos::account end (account.number () + 1);
//            for (auto i (node.store.pending_begin (transaction, logos::pending_key (account, 0))), n (node.store.pending_begin (transaction, logos::pending_key (end, 0))); i != n && peers_l.size () < count; ++i)
//            {
//                logos::pending_key key (i->first);
//                if (threshold.is_zero () && !source)
//                {
//                    boost::property_tree::ptree entry;
//                    entry.put ("", key.hash.to_string ());
//                    peers_l.push_back (std::make_pair ("", entry));
//                }
//                else
//                {
//                    logos::pending_info info (i->second);
//                    if (info.amount.number () >= threshold.number ())
//                    {
//                        if (source)
//                        {
//                            boost::property_tree::ptree pending_tree;
//                            pending_tree.put ("amount", info.amount.number ().convert_to<std::string> ());
//                            pending_tree.put ("source", info.source.to_account ());
//                            peers_l.add_child (key.hash.to_string (), pending_tree);
//                        }
//                        else
//                        {
//                            peers_l.put (key.hash.to_string (), info.amount.number ().convert_to<std::string> ());
//                        }
//                    }
//                }
//            }
//            pending.add_child (account.to_account (), peers_l);
//        }
//        else
//        {
//            error_response (response, "Bad account number");
//        }
//    }
//    response_l.add_child ("blocks", pending);
//    response (response_l);
}

void logos::rpc_handler::available_supply ()
{
//    auto genesis_balance (node.balance (logos::genesis_account)); // Cold storage genesis
//    auto landing_balance (node.balance (logos::account ("059F68AAB29DE0D3A27443625C7EA9CDDB6517A8B76FE37727EF6A4D76832AD5"))); // Active unavailable account
//    auto faucet_balance (node.balance (logos::account ("8E319CE6F3025E5B2DF66DA7AB1467FE48F1679C13DD43BFDB29FA2E9FC40D3B"))); // Faucet account
//    auto burned_balance ((node.balance_pending (logos::account (0))).second); // Burning 0 account
//    auto available (logos::genesis_amount - genesis_balance - landing_balance - faucet_balance - burned_balance);
//    boost::property_tree::ptree response_l;
//    response_l.put ("available", available.convert_to<std::string> ());
//    response (response_l);
}

void logos::rpc_handler::request_blocks ()
{
    consensus_blocks<ApprovedRB>();
}

void logos::rpc_handler::request_blocks_latest ()
{
    std::string delegate_id_text (request.get<std::string> ("delegate_id"));
    uint64_t delegate_id;
    if (decode_unsigned(delegate_id_text, delegate_id))
    {
        error_response (response, "Bad delegate ID");
    }
    if (delegate_id >= static_cast<uint64_t>(NUM_DELEGATES))
    {
        error_response (response, "Delegate ID out of range");
    }

    std::string count_text (request.get<std::string> ("count"));
    uint64_t count;
    if (decode_unsigned(count_text, count))
    {
        error_response (response, "Invalid count limit");
    }

    // Use provided head hash string, or get delegate batch tip
    auto head_str (request.get_optional<std::string> ("head"));
    BlockHash hash;
    ApprovedRB batch;
    if (head_str)
    {
        if (hash.decode_hex (*head_str))
        {
            error_response (response, "Invalid block hash.");
        }
        if (node.store.request_block_get(hash, batch))
        {
            error_response (response, "Block not found.");
        }
    }
    else
    {
        // Retrieve epoch number to get batch tip
        // indirectly figure out if we are in stale epoch
        auto epoch_number_stored (node.store.epoch_number_stored());

        auto tip_exists (false);
        Tip tip;
        tip_exists = !node.store.request_tip_get(static_cast<uint8_t>(delegate_id), epoch_number_stored + 2, tip);
        hash = tip.digest;
        // if exists, then we are in epoch i + 1 but epoch block i hasn't been persisted yet

        // otherwise, block i has been persisted, i.e. we are at least 1 MB interval past epoch start
        if (!tip_exists)
        {
            Tip tip;
            if (node.store.request_tip_get(static_cast<uint8_t>(delegate_id), epoch_number_stored + 1, tip))
            {
                error_response (response, "Internal data corruption: request block tip doesn't exist.");
            }
            hash = tip.digest;
        }
    }

    boost::property_tree::ptree response_l;
    boost::property_tree::ptree response_batch_blocks;
    BlockHash prev_hash;
    while (!hash.is_zero() && count > 0)
    {
        auto retrieve_prev_batch ([&]() -> bool {
            if (node.store.request_block_get(hash, batch))
            {
                return false;
            }
            boost::property_tree::ptree response_batch;
            batch.SerializeJson (response_batch);
            response_batch_blocks.push_back(std::make_pair("", response_batch));
            prev_hash = batch.previous;
            return true;
        });

        if (!retrieve_prev_batch())
        {
            error_response (response, "Internal data corruption: batch not found for hash " + hash.to_string());
        }

        // Check if there is a gap in the request block chain
        if (prev_hash.is_zero() && batch.epoch_number > GENESIS_EPOCH + 1)
        {
            // Attempt to get old epoch tip
            Tip tip;
            if (node.store.request_tip_get(static_cast<uint8_t>(delegate_id), batch.epoch_number - 1, tip))
            {
                // Only case a tip retrieval fails is when an epoch persistence update just happened and erased the old tip
                response_batch_blocks.pop_back();
                // Try again
                if (!retrieve_prev_batch())
                {
                    error_response (response, "Internal data corruption: batch not found.");
                }
                if (prev_hash.is_zero())
                {
                    error_response (response, "Internal data corruption: old request block tip was deleted without chain linking.");
                }
            }
            prev_hash = tip.digest;
        }
        hash = prev_hash;
        prev_hash.clear();
        count--;
    }

    response_l.add_child ("batch_blocks", response_batch_blocks);
    response_l.put ("delegate_id", delegate_id);
    if (!hash.is_zero())
    {
        response_l.put ("previous", hash.to_string());
    }
    response (response_l);
}

void logos::rpc_handler::block ()
{
    auto res = block(request,node.store);
    if(!res.error)
    {
        response(res.contents);
    }
    else
    {
        error_response(response,res.error_msg);
    }
}


void logos::rpc_handler::blocks ()
{
    auto res = blocks(request,node.store);
    if(!res.error)
    {
        response(res.contents);
    }
    else
    {
        error_response(response,res.error_msg);
    }
}

void logos::rpc_handler::blocks_exist ()
{
    auto res = blocks_exist(request,node.store);
    if(!res.error)
    {
        response(res.contents);
    }
    else
    {
        error_response(response,res.error_msg);
    }
}

void logos::rpc_handler::block_account ()
{
//    std::string hash_text (request.get<std::string> ("hash"));
//    logos::block_hash hash;
//    if (!hash.decode_hex (hash_text))
//    {
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        if (node.store.block_exists (transaction, hash))
//        {
////            boost::property_tree::ptree response_l;
////            auto account (node.ledger.account (transaction, hash));
////            response_l.put ("account", account.to_account ());
////            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Block not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Invalid block hash");
//    }
}

void logos::rpc_handler::block_create ()
{
    //TODO: refactor this function to make it cleaner/shorter
    using namespace request::fields;
    logos::uint256_union wallet (0);
    boost::optional<std::string> wallet_text (request.get_optional<std::string> (WALLET));
    if (wallet_text.is_initialized ())
    {
        auto error (wallet.decode_hex (wallet_text.get ()));
        if (error)
        {
            error_response (response, "Bad wallet number");
        }
    }
    AccountAddress origin(0);
    boost::optional<std::string> origin_text (request.get_optional<std::string> (ORIGIN));
    if (origin_text.is_initialized ())
    {
        auto error (origin.decode_account (origin_text.get ()));
        if (error)
        {
            error_response (response, "Bad account number");
        }
    }

    logos::raw_key prv;
    prv.data.clear ();
    BlockHash previous (0);
//    if (wallet != 0 && origin != 0)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            auto unlock_check (existing->second->store.valid_password (transaction));
//            if (unlock_check)
//            {
//                auto account_check (existing->second->store.find (transaction, origin));
//                if (account_check != existing->second->store.end ())
//                {
//                    existing->second->store.fetch (transaction, origin, prv);
//                    //previous = node.ledger.latest (transaction, origin);
//                }
//                else
//                {
//                    error_response (response, "Account not found in wallet");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet is locked");
//            }
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }

    boost::optional<std::string> prv_text(request.get_optional<std::string>(PRIVATE_KEY));
    if(prv_text.is_initialized())
    {
        bool error = prv.data.decode_hex(prv_text.get());
        if(error)
        {
            error_response(response, "error decoding private key");
            return;
        }
    }
    if (prv.data != 0)
    {
        AccountPubKey pub;
        ed25519_publickey (prv.data.data (), pub.data ());

        // Check for incorrect account key
        if (origin_text.is_initialized ())
        {
            if (origin != pub)
            {
                error_response (response, "Incorrect key for given account");
            }
        }
        else
        {
            origin = pub;
            request.put(ORIGIN,origin.to_account());
        }

        std::string pub_key_str;
        pub.encode_hex(pub_key_str);
        request.put(PUBLIC_KEY,pub_key_str);
        bool error = false;
        RequestType type = GetRequestType(error,request.get<std::string>(TYPE));
        if(error)
        {
            error_response(response,"Unable to decode request type");
            return;
        }
        if(type == RequestType::Issuance)
        {
            auto token_id_str = request.get_optional<std::string>(TOKEN_ID);
            if(!token_id_str.is_initialized())
            {
                request.put(TOKEN_ID,"placeholder");
            }
        }

        logos::account_info info;
        auto account_error(node.store.account_get(origin, info));
        if(account_error)
        {
            error_response (response, "logos::rpc_handler::block_create - Unable to find account.");
        }
        request.put(SEQUENCE, info.block_count);



        auto created_request = DeserializeRequest(error, request);
        if(error)
        {

            std::stringstream ss;
            boost::property_tree::json_parser::write_json(ss, request);
            error_response (response, "error creating request from: \n" + ss.str());
            return;

        }

        std::shared_ptr<logos::Account> info_ptr;
        if(!node.store.account_get(created_request->GetAccount(),info_ptr))
        {
            created_request->sequence = info_ptr->block_count;
            created_request->previous = info_ptr->head;
            created_request->Sign(prv.data, pub);
        }

        boost::property_tree::ptree response_l;
        response_l.put ("hash", created_request->GetHash ().to_string ());
        std::string contents(created_request->ToJson());
        response_l.put ("request", contents);
        response (response_l);

    }
    else
    {
        error_response (response, "Private key or local wallet and account required");
    }
}

void logos::rpc_handler::block_hash ()
{
//    std::string block_text (request.get<std::string> ("block"));
//    boost::property_tree::ptree block_l;
//    std::stringstream block_stream (block_text);
//    boost::property_tree::read_json (block_stream, block_l);
//    block_l.put ("signature", "0");
//    block_l.put ("work", "0");
//    auto block (logos::deserialize_block_json (block_l));
//    if (block != nullptr)
//    {
//        boost::property_tree::ptree response_l;
//        response_l.put ("hash", block->hash ().to_string ());
//        response (response_l);
//    }
//    else
//    {
//        error_response (response, "Block is invalid");
//    }
}

void logos::rpc_handler::successors ()
{
//    std::string block_text (request.get<std::string> ("block"));
//    std::string count_text (request.get<std::string> ("count"));
//    logos::block_hash block;
//    if (!block.decode_hex (block_text))
//    {
//        uint64_t count;
//        if (!decode_unsigned (count_text, count))
//        {
//            boost::property_tree::ptree response_l;
//            boost::property_tree::ptree blocks;
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            while (!block.is_zero () && blocks.size () < count)
//            {
//                auto block_l (node.store.block_get (transaction, block));
//                if (block_l != nullptr)
//                {
//                    boost::property_tree::ptree entry;
//                    entry.put ("", block.to_string ());
//                    blocks.push_back (std::make_pair ("", entry));
//                    block = node.store.block_successor (transaction, block);
//                }
//                else
//                {
//                    block.clear ();
//                }
//            }
//            response_l.add_child ("blocks", blocks);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Invalid count limit");
//        }
//    }
//    else
//    {
//        error_response (response, "Invalid block hash");
//    }
}

void logos::rpc_handler::bootstrap ()
{
    std::string address_text = request.get<std::string> ("address");
    std::string port_text = request.get<std::string> ("port");
    boost::system::error_code ec;
    auto address (boost::asio::ip::address_v6::from_string (address_text, ec));
    if (!ec)
    {
        uint16_t port;
        if (!logos::parse_port (port_text, port))
        {
            node.bootstrap_initiator.bootstrap ({}, logos::endpoint (address, port));
            boost::property_tree::ptree response_l;
            response_l.put ("success", "");
            response (response_l);
        }
        else
        {
            error_response (response, "Invalid port");
        }
    }
    else
    {
        error_response (response, "Invalid address");
    }
}

void logos::rpc_handler::bootstrap_any ()
{
    node.bootstrap_initiator.bootstrap ();
    boost::property_tree::ptree response_l;
    response_l.put ("success", "");
    response (response_l);
}

void logos::rpc_handler::bootstrap_progress ()
{
    auto bp = node.CreateBootstrapProgress();
    boost::property_tree::ptree response_l;
    bp.SerializeJson(response_l);
    response (response_l);
}

void logos::rpc_handler::chain ()
{
//    std::string block_text (request.get<std::string> ("block"));
//    std::string count_text (request.get<std::string> ("count"));
//    logos::block_hash block;
//    if (!block.decode_hex (block_text))
//    {
//        uint64_t count;
//        if (!decode_unsigned (count_text, count))
//        {
//            boost::property_tree::ptree response_l;
//            boost::property_tree::ptree blocks;
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            while (!block.is_zero () && blocks.size () < count)
//            {
//                auto block_l (node.store.block_get (transaction, block));
//                if (block_l != nullptr)
//                {
//                    boost::property_tree::ptree entry;
//                    entry.put ("", block.to_string ());
//                    blocks.push_back (std::make_pair ("", entry));
//                    block = block_l->previous ();
//                }
//                else
//                {
//                    block.clear ();
//                }
//            }
//            response_l.add_child ("blocks", blocks);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Invalid count limit");
//        }
//    }
//    else
//    {
//        error_response (response, "Invalid block hash");
//    }
}

template <typename  CT>
void logos::rpc_handler::consensus_blocks ()
{
    std::vector<std::string> hashes;
    boost::property_tree::ptree response_l;
    boost::property_tree::ptree blocks;
    logos::transaction transaction (node.store.environment, nullptr, false);
    for (boost::property_tree::ptree::value_type & hashes : request.get_child ("hashes"))
    {
        std::string hash_text = hashes.second.data ();
        BlockHash hash;
        if (hash.decode_hex (hash_text))
        {
            error_response (response, "Bad hash number");
        }
        CT response_block;
        if (node.store.consensus_block_get(hash, response_block))
        {
            error_response (response, "Block not found");
        }
        boost::property_tree::ptree contents;
        response_block.SerializeJson (contents);
        contents.put ("hash", hash_text);
        blocks.push_back (std::make_pair("", contents));
    }
    response_l.add_child ("blocks", blocks);
    response (response_l);
}

void logos::rpc_handler::delegators ()
{
//    std::string account_text (request.get<std::string> ("account"));
//    logos::account account;
//    auto error (account.decode_account (account_text));
//    if (!error)
//    {
//        boost::property_tree::ptree response_l;
//        boost::property_tree::ptree delegators;
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        for (auto i (node.store.latest_begin (transaction)), n (node.store.latest_end ()); i != n; ++i)
//        {
//            bool error = false;
//            logos::account_info info (error, i->second);
//            if(error)
//            {
//                error_response (response, "account_info deserialize");
//            }
//            auto block (node.store.block_get (transaction, info.governance_subchain_head));
//            assert (block != nullptr);
//            if (block->representative () == account)
//            {
//                std::string balance;
//                logos::uint128_union (info.GetBalance()).encode_dec (balance);
//                delegators.put (logos::account (i->first.uint256 ()).to_account (), balance);
//            }
//        }
//        response_l.add_child ("delegators", delegators);
//        response (response_l);
//    }
//    else
//    {
//        error_response (response, "Bad account number");
//    }
}

void logos::rpc_handler::delegators_count ()
{
//    std::string account_text (request.get<std::string> ("account"));
//    logos::account account;
//    auto error (account.decode_account (account_text));
//    if (!error)
//    {
//        uint64_t count (0);
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        for (auto i (node.store.latest_begin (transaction)), n (node.store.latest_end ()); i != n; ++i)
//        {
//            bool error = false;
//            logos::account_info info (error, i->second);
//            if(error)
//            {
//                error_response (response, "account_info deserialize");
//            }
//            auto block (node.store.block_get (transaction, info.governance_subchain_head));
//            assert (block != nullptr);
//            if (block->representative () == account)
//            {
//                ++count;
//            }
//        }
//        boost::property_tree::ptree response_l;
//        response_l.put ("count", std::to_string (count));
//        response (response_l);
//    }
//    else
//    {
//        error_response (response, "Bad account number");
//    }
}

void logos::rpc_handler::deterministic_key ()
{
    std::string seed_text (request.get<std::string> ("seed"));
    std::string index_text (request.get<std::string> ("index"));
    logos::raw_key seed;
    auto error (seed.data.decode_hex (seed_text));
    if (!error)
    {
        uint64_t index_a;
        if (!decode_unsigned (index_text, index_a))
        {
            logos::uint256_union index (index_a);
            logos::uint256_union prv;
            blake2b_state hash;
            blake2b_init (&hash, prv.bytes.size ());
            blake2b_update (&hash, seed.data.bytes.data (), seed.data.bytes.size ());
            blake2b_update (&hash, reinterpret_cast<uint8_t *> (&index.dwords[7]), sizeof (uint32_t));
            blake2b_final (&hash, prv.bytes.data (), prv.bytes.size ());
            boost::property_tree::ptree response_l;
            logos::uint256_union pub;
            ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
            response_l.put ("private", prv.to_string ());
            response_l.put ("public", pub.to_string ());
            response_l.put ("account", pub.to_account ());
            response (response_l);
        }
        else
        {
            error_response (response, "Invalid index");
        }
    }
    else
    {
        error_response (response, "Bad seed");
    }
}

void logos::rpc_handler::epochs ()
{
    consensus_blocks<ApprovedEB>();
}

void logos::rpc_handler::epochs_latest ()
{
    std::string count_text (request.get<std::string> ("count"));
    uint64_t count;
    if (decode_unsigned (count_text, count))
    {
        error_response (response, "Invalid count limit");
    }

    // Use provided head hash string, or get delegate batch tip
    auto head_str (request.get_optional<std::string> ("head"));
    BlockHash hash;
    ApprovedEB epoch;
    if (head_str)
    {
        if (hash.decode_hex (*head_str))
        {
            error_response (response, "Invalid block hash.");
        }
        if (node.store.epoch_get(hash, epoch))
        {
            error_response (response, "Epoch not found.");
        }
    }
    else
    {
        Tip tip;
        auto tip_exists (!node.store.epoch_tip_get(tip));
        hash = tip.digest;
        assert (tip_exists);
    }

    boost::property_tree::ptree response_l;
    boost::property_tree::ptree response_epochs;

    while (!hash.is_zero() && count > 0)
    {
        if (node.store.epoch_get(hash, epoch))
        {
            error_response (response, "Internal data corruption");
        }
        boost::property_tree::ptree response_epoch;
        epoch.SerializeJson (response_epoch);
        response_epochs.push_back(std::make_pair("", response_epoch));
        hash = epoch.previous;
        count--;
    }
    response_l.add_child("epochs", response_epochs);
    if (!hash.is_zero())
    {
        response_l.put("previous", hash.to_string());
    }

    response (response_l);
}

void logos::rpc_handler::frontiers ()
{
//    std::string account_text (request.get<std::string> ("account"));
//    std::string count_text (request.get<std::string> ("count"));
//    logos::account start;
//    if (!start.decode_account (account_text))
//    {
//        uint64_t count;
//        if (!decode_unsigned (count_text, count))
//        {
//            boost::property_tree::ptree response_l;
//            boost::property_tree::ptree frontiers;
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n && frontiers.size () < count; ++i)
//            {
//                bool error = false;
//                frontiers.put (logos::account (i->first.uint256 ()).to_account (), logos::account_info (error, i->second).head.to_string ());
//                if(error)
//                {
//                    error_response (response, "account_info deserialize");
//                }
//            }
//            response_l.add_child ("frontiers", frontiers);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Invalid count limit");
//        }
//    }
//    else
//    {
//        error_response (response, "Invalid starting account");
//    }
}

void logos::rpc_handler::account_count ()
{
//    logos::transaction transaction (node.store.environment, nullptr, false);
//    auto size (node.store.account_count (transaction));
//    boost::property_tree::ptree response_l;
//    response_l.put ("count", std::to_string (size));
//    response (response_l);
}

void logos::rpc_handler::account_history ()
{
    std::string account_text = request.get<std::string> ("account");
    std::string count_text (request.get<std::string> ("count"));
    bool output_raw (request.get_optional<bool> ("raw") == true);
    auto error (false);
    logos::block_hash request_hash, receive_hash;
    auto head_str (request.get_optional<std::string> ("head"));
    logos::transaction transaction (node.store.environment, nullptr, false);

    // get account
    logos::uint256_union account;
    if (account.decode_account (account_text))
    {
        error_response (response, "Bad account number");
    }

    std::shared_ptr<logos::Account> info;
    if (node.store.account_get (account, info, transaction))
    {
        error_response (response, "Account not found.");
    }

    request_hash = info->head;
    receive_hash = info->receive_head;
    // get optional send head block
    if (head_str)
    {
        if (request_hash.decode_hex (*head_str))
        {
            error_response (response, "Invalid block hash");
        }
    }

    // get count + offset
    uint64_t count;
    if (decode_unsigned (count_text, count))
    {
        error_response (response, "Invalid count limit");
    }
    uint64_t offset = 0;
    auto offset_text (request.get_optional<std::string> ("offset"));
    if (offset_text && decode_unsigned (*offset_text, offset))
    {
        error_response (response, "Invalid offset");
    }

    boost::property_tree::ptree response_l;
    boost::property_tree::ptree history;
    response_l.put ("account", account_text);
    std::shared_ptr<Request> request_ptr;
    ReceiveBlock receive_data;
    std::shared_ptr<Request> receive_source_ptr;
    bool request_not_found (node.store.request_get(request_hash, request_ptr, transaction));
    bool receive_not_found (node.store.receive_get (receive_hash, receive_data, transaction));
    bool put_request (false);
    uint64_t timestamp;
    ApprovedRB batch;
    while (!(request_not_found && receive_not_found) && count > 0)
    {
        // compare timestamp of request and receive, serialize whichever is more recent
        if (receive_not_found)  // at end of receive chain?
        {
            put_request = true;
            timestamp = node.store.request_block_get(request_ptr->locator.hash, batch) ? 0 : batch.timestamp;
        }
        else
        {
            // get receive's source send
            if (node.store.request_get(receive_data.source_hash, receive_source_ptr, transaction))
            {
                error_response (response, "Internal error: send not found for receive.");
            }
            // at end of request chain?
            if (request_not_found)
            {
                put_request = false;
                timestamp = node.store.request_block_get(receive_source_ptr->locator.hash, batch) ? 0 : batch.timestamp;
            }
            // compare timestamps
            else
            {
                // request timestamp
                auto request_ts (node.store.request_block_get(request_ptr->locator.hash, batch) ? 0 : batch.timestamp);
                // receive timestamp
                auto recv_ts (node.store.request_block_get(receive_source_ptr->locator.hash, batch) ? 0 : batch.timestamp);
                put_request = request_ts >= recv_ts;
                timestamp = request_ts >= recv_ts ? request_ts : recv_ts;
            }
        }

        if (offset > 0)
        {
            --offset;
        }
        else
        {
            boost::property_tree::ptree entry;
            boost::property_tree::ptree contents = (put_request ? request_ptr : receive_source_ptr)->SerializeJson();
            contents.put("timestamp", std::to_string(timestamp));
            history.push_back (std::make_pair("", contents));
            --count;
        }
        if (put_request)
        {
            request_hash = request_ptr->previous;
            request_not_found = node.store.request_get(request_hash, request_ptr, transaction);
        }
        else
        {
            receive_hash = receive_data.previous;
            receive_not_found = node.store.receive_get (receive_hash, receive_data, transaction);
        }
    }
    response_l.add_child ("history", history);
    if (!request_hash.is_zero ())  // TODO: fix pagination
    {
        response_l.put ("previous", request_hash.to_string ());
    }
    response (response_l);
}

void logos::rpc_handler::key_create ()
{
    boost::property_tree::ptree response_l;
    logos::keypair pair;
    response_l.put ("private", pair.prv.data.to_string ());
    response_l.put ("public", pair.pub.to_string ());
    response_l.put ("account", pair.pub.to_account ());
    response (response_l);
}

void logos::rpc_handler::key_expand ()
{
    std::string key_text (request.get<std::string> ("key"));
    logos::uint256_union prv;
    auto error (prv.decode_hex (key_text));
    if (!error)
    {
        boost::property_tree::ptree response_l;
        logos::uint256_union pub;
        ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
        response_l.put ("private", prv.to_string ());
        response_l.put ("public", pub.to_string ());
        response_l.put ("account", pub.to_account ());
        response (response_l);
    }
    else
    {
        error_response (response, "Bad private key");
    }
}

void logos::rpc_handler::ledger ()
{
//    if (rpc.config.enable_control)
//    {
//        logos::account start (0);
//        uint64_t count (std::numeric_limits<uint64_t>::max ());
//        boost::optional<std::string> account_text (request.get_optional<std::string> ("account"));
//        if (account_text.is_initialized ())
//        {
//            auto error (start.decode_account (account_text.get ()));
//            if (error)
//            {
//                error_response (response, "Invalid starting account");
//            }
//        }
//        boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
//        if (count_text.is_initialized ())
//        {
//            auto error_count (decode_unsigned (count_text.get (), count));
//            if (error_count)
//            {
//                error_response (response, "Invalid count limit");
//            }
//        }
//        uint64_t modified_since (0);
//        boost::optional<std::string> modified_since_text (request.get_optional<std::string> ("modified_since"));
//        if (modified_since_text.is_initialized ())
//        {
//            modified_since = strtoul (modified_since_text.get ().c_str (), NULL, 10);
//        }
//        const bool sorting = request.get<bool> ("sorting", false);
//        const bool representative = request.get<bool> ("representative", false);
//        const bool weight = request.get<bool> ("weight", false);
//        const bool pending = request.get<bool> ("pending", false);
//        boost::property_tree::ptree response_a;
//        boost::property_tree::ptree response_l;
//        boost::property_tree::ptree accounts;
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        if (!sorting) // Simple
//        {
//            for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n && accounts.size () < count; ++i)
//            {
//                bool error = false;
//                logos::account_info info (error, i->second);
//                if(error)
//                {
//                    error_response (response, "account_info deserialize");
//                }
//                if (info.modified >= modified_since)
//                {
//                    logos::account account (i->first.uint256 ());
//                    boost::property_tree::ptree response_l;
//                    response_l.put ("frontier", info.head.to_string ());
//                    response_l.put ("open_block", info.open_block.to_string ());
//                    response_l.put ("representative_block", info.governance_subchain_head.to_string ());
//                    std::string balance;
//                    logos::uint128_union (info.GetBalance()).encode_dec (balance);
//                    response_l.put ("balance", balance);
//                    response_l.put ("modified_timestamp", std::to_string (info.modified));
//                    response_l.put ("request_count", std::to_string (info.block_count));
//                    if (representative)
//                    {
//                        auto block (node.store.block_get (transaction, info.governance_subchain_head));
//                        assert (block != nullptr);
//                        response_l.put ("representative", block->representative ().to_account ());
//                    }
//                    if (weight)
//                    {
////                        auto account_weight (node.ledger.weight (transaction, account));
////                        response_l.put ("weight", account_weight.convert_to<std::string> ());
//                    }
//                    if (pending)
//                    {
//                        //auto account_pending (node.ledger.account_pending (transaction, account));
//                        //response_l.put ("pending", account_pending.convert_to<std::string> ());
//                    }
//                    accounts.push_back (std::make_pair (account.to_account (), response_l));
//                }
//            }
//        }
//        else // Sorting
//        {
//            std::vector<std::pair<logos::uint128_union, logos::account>> ledger_l;
//            for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n; ++i)
//            {
//                bool error = false;
//                logos::account_info info (error, i->second);
//                if(error)
//                {
//                    error_response (response, "account_info deserialize");
//                }
//                logos::uint128_union balance (info.GetBalance());
//                if (info.modified >= modified_since)
//                {
//                    ledger_l.push_back (std::make_pair (balance, logos::account (i->first.uint256 ())));
//                }
//            }
//            std::sort (ledger_l.begin (), ledger_l.end ());
//            std::reverse (ledger_l.begin (), ledger_l.end ());
//            logos::account_info info;
//            for (auto i (ledger_l.begin ()), n (ledger_l.end ()); i != n && accounts.size () < count; ++i)
//            {
//                node.store.account_get (transaction, i->second, info);
//                logos::account account (i->second);
//                response_l.put ("frontier", info.head.to_string ());
//                response_l.put ("open_block", info.open_block.to_string ());
//                response_l.put ("representative_block", info.governance_subchain_head.to_string ());
//                std::string balance;
//                (i->first).encode_dec (balance);
//                response_l.put ("balance", balance);
//                response_l.put ("modified_timestamp", std::to_string (info.modified));
//                response_l.put ("request_count", std::to_string (info.block_count));
//                if (representative)
//                {
//                    auto block (node.store.block_get (transaction, info.governance_subchain_head));
//                    assert (block != nullptr);
//                    response_l.put ("representative", block->representative ().to_account ());
//                }
//                if (weight)
//                {
////                    auto account_weight (node.ledger.weight (transaction, account));
////                    response_l.put ("weight", account_weight.convert_to<std::string> ());
//                }
//                if (pending)
//                {
////                    auto account_pending (node.ledger.account_pending (transaction, account));
////                    response_l.put ("pending", account_pending.convert_to<std::string> ());
//                }
//                accounts.push_back (std::make_pair (account.to_account (), response_l));
//            }
//        }
//        response_a.add_child ("accounts", accounts);
//        response (response_a);
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::micro_blocks ()
{
    consensus_blocks<ApprovedMB>();
}

void logos::rpc_handler::micro_blocks_latest ()
{
    std::string count_text (request.get<std::string> ("count"));
    uint64_t count;
    if (decode_unsigned (count_text, count))
    {
        error_response (response, "Invalid count limit");
    }

    // Use provided head hash string, or get delegate batch tip
    auto head_str (request.get_optional<std::string> ("head"));
    ApprovedMB micro_block;
    BlockHash hash;
    if (head_str)
    {
        if (hash.decode_hex (*head_str))
        {
            error_response (response, "Invalid block hash.");
        }
        if (node.store.micro_block_get(hash, micro_block))
        {
            error_response (response, "Micro block not found.");
        }
    }
    else
    {
        Tip tip;
        auto tip_exists (!node.store.micro_block_tip_get(tip));
        hash = tip.digest;
        assert (tip_exists);
    }

    boost::property_tree::ptree response_l;
    boost::property_tree::ptree response_micro_blocks;

    while (!hash.is_zero() && count > 0)
    {
        if (node.store.micro_block_get(hash, micro_block))
        {
            error_response (response, "Internal data corruption");
        }
        boost::property_tree::ptree response_micro_block;
        micro_block.SerializeJson (response_micro_block);
        response_micro_blocks.push_back(std::make_pair("", response_micro_block));
        hash = micro_block.previous;
        count--;
    }
    response_l.add_child("micro_blocks", response_micro_blocks);

    if (!hash.is_zero ())
    {
        response_l.put("previous", hash.to_string());
    }
    response (response_l);
}

void logos::rpc_handler::mrai_from_raw ()
{
    std::string amount_text (request.get<std::string> ("amount"));
    logos::uint128_union amount;
    if (!amount.decode_dec (amount_text))
    {
        auto result (amount.number () / logos::Mlgs_ratio);
        boost::property_tree::ptree response_l;
        response_l.put ("amount", result.convert_to<std::string> ());
        response (response_l);
    }
    else
    {
        error_response (response, "Bad amount number");
    }
}

void logos::rpc_handler::mrai_to_raw ()
{
    std::string amount_text (request.get<std::string> ("amount"));
    logos::uint128_union amount;
    if (!amount.decode_dec (amount_text))
    {
        auto result (amount.number () * logos::Mlgs_ratio);
        if (result > amount.number ())
        {
            boost::property_tree::ptree response_l;
            response_l.put ("amount", result.convert_to<std::string> ());
            response (response_l);
        }
        else
        {
            error_response (response, "Amount too big");
        }
    }
    else
    {
        error_response (response, "Bad amount number");
    }
}

void logos::rpc_handler::krai_from_raw ()
{
    std::string amount_text (request.get<std::string> ("amount"));
    logos::uint128_union amount;
    if (!amount.decode_dec (amount_text))
    {
        auto result (amount.number () / logos::klgs_ratio);
        boost::property_tree::ptree response_l;
        response_l.put ("amount", result.convert_to<std::string> ());
        response (response_l);
    }
    else
    {
        error_response (response, "Bad amount number");
    }
}

void logos::rpc_handler::krai_to_raw ()
{
    std::string amount_text (request.get<std::string> ("amount"));
    logos::uint128_union amount;
    if (!amount.decode_dec (amount_text))
    {
        auto result (amount.number () * logos::klgs_ratio);
        if (result > amount.number ())
        {
            boost::property_tree::ptree response_l;
            response_l.put ("amount", result.convert_to<std::string> ());
            response (response_l);
        }
        else
        {
            error_response (response, "Amount too big");
        }
    }
    else
    {
        error_response (response, "Bad amount number");
    }
}

void logos::rpc_handler::password_change ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            logos::transaction transaction (node.store.environment, nullptr, true);
//            boost::property_tree::ptree response_l;
//            std::string password_text (request.get<std::string> ("password"));
//            auto error (existing->second->store.rekey (transaction, password_text));
//            response_l.put ("changed", error ? "0" : "1");
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::password_enter ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            boost::property_tree::ptree response_l;
//            std::string password_text (request.get<std::string> ("password"));
//            auto error (existing->second->enter_password (password_text));
//            response_l.put ("valid", error ? "0" : "1");
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::password_valid (bool wallet_locked = false)
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            boost::property_tree::ptree response_l;
//            auto valid (existing->second->store.valid_password (transaction));
//            if (!wallet_locked)
//            {
//                response_l.put ("valid", valid ? "1" : "0");
//            }
//            else
//            {
//                response_l.put ("locked", valid ? "0" : "1");
//            }
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::pending ()
{
//    std::string account_text (request.get<std::string> ("account"));
//    logos::account account;
//    if (!account.decode_account (account_text))
//    {
//        uint64_t count (std::numeric_limits<uint64_t>::max ());
//        logos::uint128_union threshold (0);
//        boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
//        if (count_text.is_initialized ())
//        {
//            auto error (decode_unsigned (count_text.get (), count));
//            if (error)
//            {
//                error_response (response, "Invalid count limit");
//            }
//        }
//        boost::optional<std::string> threshold_text (request.get_optional<std::string> ("threshold"));
//        if (threshold_text.is_initialized ())
//        {
//            auto error_threshold (threshold.decode_dec (threshold_text.get ()));
//            if (error_threshold)
//            {
//                error_response (response, "Bad threshold number");
//            }
//        }
//        const bool source = request.get<bool> ("source", false);
//        boost::property_tree::ptree response_l;
//        boost::property_tree::ptree peers_l;
//        {
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            logos::account end (account.number () + 1);
//            for (auto i (node.store.pending_begin (transaction, logos::pending_key (account, 0))), n (node.store.pending_begin (transaction, logos::pending_key (end, 0))); i != n && peers_l.size () < count; ++i)
//            {
//                logos::pending_key key (i->first);
//                if (threshold.is_zero () && !source)
//                {
//                    boost::property_tree::ptree entry;
//                    entry.put ("", key.hash.to_string ());
//                    peers_l.push_back (std::make_pair ("", entry));
//                }
//                else
//                {
//                    logos::pending_info info (i->second);
//                    if (info.amount.number () >= threshold.number ())
//                    {
//                        if (source)
//                        {
//                            boost::property_tree::ptree pending_tree;
//                            pending_tree.put ("amount", info.amount.number ().convert_to<std::string> ());
//                            pending_tree.put ("source", info.source.to_account ());
//                            peers_l.add_child (key.hash.to_string (), pending_tree);
//                        }
//                        else
//                        {
//                            peers_l.put (key.hash.to_string (), info.amount.number ().convert_to<std::string> ());
//                        }
//                    }
//                }
//            }
//        }
//        response_l.add_child ("blocks", peers_l);
//        response (response_l);
//    }
//    else
//    {
//        error_response (response, "Bad account number");
//    }
}

void logos::rpc_handler::pending_exists ()
{
//    std::string hash_text (request.get<std::string> ("hash"));
//    logos::uint256_union hash;
//    auto error (hash.decode_hex (hash_text));
//    if (!error)
//    {
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        auto block (node.store.block_get (transaction, hash));
//        if (block != nullptr)
//        {
//            auto exists (false);
//            //auto destination (node.ledger.block_destination (transaction, *block));
////            if (!destination.is_zero ())
////            {
////                exists = node.store.pending_exists (transaction, logos::pending_key (destination, hash));
////            }
//            boost::property_tree::ptree response_l;
//            response_l.put ("exists", exists ? "1" : "0");
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Block not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad hash number");
//    }
}

void logos::rpc_handler::payment_begin ()
{
//    std::string id_text (request.get<std::string> ("wallet"));
//    logos::uint256_union id;
//    if (!id.decode_hex (id_text))
//    {
//        auto existing (node.wallets.items.find (id));
//        if (existing != node.wallets.items.end ())
//        {
//            logos::transaction transaction (node.store.environment, nullptr, true);
//            std::shared_ptr<logos::wallet> wallet (existing->second);
//            if (wallet->store.valid_password (transaction))
//            {
//                logos::account account (0);
//                do
//                {
//                    auto existing (wallet->free_accounts.begin ());
//                    if (existing != wallet->free_accounts.end ())
//                    {
//                        account = *existing;
//                        wallet->free_accounts.erase (existing);
//                        if (wallet->store.find (transaction, account) == wallet->store.end ())
//                        {
//                            BOOST_LOG (node.log) << boost::str (boost::format ("Transaction wallet %1% externally modified listing account %2% as free but no longer exists") % id.to_string () % account.to_account ());
//                            account.clear ();
//                        }
//                        else
//                        {
////                            if (!node.ledger.account_balance (transaction, account).is_zero ())
////                            {
////                                BOOST_LOG (node.log) << boost::str (boost::format ("Skipping account %1% for use as a transaction account: non-zero balance") % account.to_account ());
////                                account.clear ();
////                            }
//                        }
//                    }
//                    else
//                    {
//                        account = wallet->deterministic_insert (transaction);
//                        break;
//                    }
//                } while (account.is_zero ());
//                if (!account.is_zero ())
//                {
//                    boost::property_tree::ptree response_l;
//                    response_l.put ("account", account.to_account ());
//                    response (response_l);
//                }
//                else
//                {
//                    error_response (response, "Unable to create transaction account");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet locked");
//            }
//        }
//        else
//        {
//            error_response (response, "Unable to find wallets");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::payment_init ()
{
//    std::string id_text (request.get<std::string> ("wallet"));
//    logos::uint256_union id;
//    if (!id.decode_hex (id_text))
//    {
//        logos::transaction transaction (node.store.environment, nullptr, true);
//        auto existing (node.wallets.items.find (id));
//        if (existing != node.wallets.items.end ())
//        {
//            auto wallet (existing->second);
//            if (wallet->store.valid_password (transaction))
//            {
//                wallet->init_free_accounts (transaction);
//                boost::property_tree::ptree response_l;
//                response_l.put ("status", "Ready");
//                response (response_l);
//            }
//            else
//            {
//                boost::property_tree::ptree response_l;
//                response_l.put ("status", "Transaction wallet locked");
//                response (response_l);
//            }
//        }
//        else
//        {
//            boost::property_tree::ptree response_l;
//            response_l.put ("status", "Unable to find transaction wallet");
//            response (response_l);
//        }
//    }
//    else
//    {
//        error_response (response, "Bad transaction wallet number");
//    }
}

void logos::rpc_handler::payment_end ()
{
//    std::string id_text (request.get<std::string> ("wallet"));
//    std::string account_text (request.get<std::string> ("account"));
//    logos::uint256_union id;
//    if (!id.decode_hex (id_text))
//    {
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        auto existing (node.wallets.items.find (id));
//        if (existing != node.wallets.items.end ())
//        {
//            auto wallet (existing->second);
//            logos::account account;
//            if (!account.decode_account (account_text))
//            {
//                auto existing (wallet->store.find (transaction, account));
//                if (existing != wallet->store.end ())
//                {
////                    if (node.ledger.account_balance (transaction, account).is_zero ())
////                    {
////                        wallet->free_accounts.insert (account);
////                        boost::property_tree::ptree response_l;
////                        response (response_l);
////                    }
////                    else
////                    {
////                        error_response (response, "Account has non-zero balance");
////                    }
//                }
//                else
//                {
//                    error_response (response, "Account not in wallet");
//                }
//            }
//            else
//            {
//                error_response (response, "Invalid account number");
//            }
//        }
//        else
//        {
//            error_response (response, "Unable to find wallet");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::payment_wait ()
{
    std::string account_text (request.get<std::string> ("account"));
    std::string amount_text (request.get<std::string> ("amount"));
    std::string timeout_text (request.get<std::string> ("timeout"));
    logos::uint256_union account;
    if (!account.decode_account (account_text))
    {
        logos::uint128_union amount;
        if (!amount.decode_dec (amount_text))
        {
            uint64_t timeout;
            if (!decode_unsigned (timeout_text, timeout))
            {
                {
                    auto observer (std::make_shared<logos::payment_observer> (response, rpc, account, amount));
                    observer->start (timeout);
                    std::lock_guard<std::mutex> lock (rpc.mutex);
                    assert (rpc.payment_observers.find (account) == rpc.payment_observers.end ());
                    rpc.payment_observers[account] = observer;
                }
                rpc.observer_action (account);
            }
            else
            {
                error_response (response, "Bad timeout number");
            }
        }
        else
        {
            error_response (response, "Bad amount number");
        }
    }
    else
    {
        error_response (response, "Bad account number");
    }
}

std::unique_ptr<Send> deserialize_StateBlock_json (boost::property_tree::ptree const & tree_a)
{
    std::unique_ptr<Send> result;
    try
    {
        auto type (tree_a.get<std::string> ("type"));
        if (type == "state")
        {
            bool error;
            std::unique_ptr<Send> obj (new Send (error, tree_a));
            if (!error)
            {
                result = std::move (obj);
            }
        }
    }
    catch (std::runtime_error const &)
    {
    }
    return result;
}

void logos::rpc_handler::process(
        std::shared_ptr<Request> request)
{

    // TODO: check work, !logos::work_validate (*request)
    auto result = node.OnRequest(request, should_buffer_request());
    auto hash = request->GetHash();

    switch(result.code)
    {
        case logos::process_result::progress:
        case logos::process_result::propagate:
            {
                boost::property_tree::ptree response_l;
                response_l.put ("hash", hash.to_string ());
                response_l.put("result", ProcessResultToString(result.code));
                response (response_l);
                break;
            }
        case logos::process_result::gap_previous:
        case logos::process_result::gap_source:
        case logos::process_result::state_block_disabled:
        case logos::process_result::old:
        case logos::process_result::bad_signature:
        case logos::process_result::negative_spend:
        case logos::process_result::unreceivable:
        case logos::process_result::not_receive_from_send:
        case logos::process_result::fork:
        case logos::process_result::account_mismatch:
        case logos::process_result::invalid_block_type:
        case logos::process_result::unknown_source_account:
        case logos::process_result::unknown_origin:
        case logos::process_result::opened_burn_account:
        case logos::process_result::already_reserved:
        case logos::process_result::initializing:
        case logos::process_result::insufficient_balance:
        case logos::process_result::not_delegate:
            {
                error_response (response,
                        ProcessResultToString(result.code));
                break;
            }
        case logos::process_result::buffered:
        case logos::process_result::buffering_done:
        case logos::process_result::pending:
            {
                boost::property_tree::ptree response_l;
                response_l.put ("result",
                        ProcessResultToString(result.code));
                response (response_l);
                break;
            }
        default:
            {
                error_response (response,
                        ProcessResultToString(result.code));
                break;
            }
    }

}

void logos::rpc_handler::process ()
{
    std::string request_text (request.get<std::string> ("request"));

    boost::property_tree::ptree request_json;
    std::stringstream block_stream (request_text);
    boost::property_tree::read_json (block_stream, request_json);
    bool error = false;
    std::shared_ptr<Request> request;
    try
    {
        request = DeserializeRequest(error, request_json);
    }
    catch(std::exception& e)
    {
        std::string msg("Error deserializing request: ");
        msg += e.what();
        error_response(response, msg);
    }


    if(!error)
    {
        switch(request->type)
        {
            case RequestType::Send:
            case RequestType::Issuance:
            case RequestType::ChangeSetting:
            case RequestType::IssueAdditional:
            case RequestType::ImmuteSetting:
            case RequestType::Revoke:
            case RequestType::AdjustUserStatus:
            case RequestType::AdjustFee:
            case RequestType::UpdateIssuerInfo:
            case RequestType::UpdateController:
            case RequestType::Burn:
            case RequestType::Distribute:
            case RequestType::WithdrawFee:
            case RequestType::WithdrawLogos:
            case RequestType::TokenSend:
            case RequestType::ElectionVote:
            case RequestType::AnnounceCandidacy:
            case RequestType::RenounceCandidacy:
            case RequestType::StartRepresenting:
            case RequestType::StopRepresenting:
            case RequestType::Proxy:
            case RequestType::Stake:
            case RequestType::Unstake:
                process(request);
                break;
            case RequestType::Claim:
                break;
            default:
                error_response(response, "Request is invalid");
        }
    }
    else
    {
        error_response (response, "Request is invalid");
    }
}

void logos::rpc_handler::rai_from_raw ()
{
    std::string amount_text (request.get<std::string> ("amount"));
    logos::uint128_union amount;
    if (!amount.decode_dec (amount_text))
    {
        auto result (amount.number () / logos::lgs_ratio);
        boost::property_tree::ptree response_l;
        response_l.put ("amount", result.convert_to<std::string> ());
        response (response_l);
    }
    else
    {
        error_response (response, "Bad amount number");
    }
}

void logos::rpc_handler::rai_to_raw ()
{
    std::string amount_text (request.get<std::string> ("amount"));
    logos::uint128_union amount;
    if (!amount.decode_dec (amount_text))
    {
        auto result (amount.number () * logos::lgs_ratio);
        if (result > amount.number ())
        {
            boost::property_tree::ptree response_l;
            response_l.put ("amount", result.convert_to<std::string> ());
            response (response_l);
        }
        else
        {
            error_response (response, "Amount too big");
        }
    }
    else
    {
        error_response (response, "Bad amount number");
    }
}

void logos::rpc_handler::receive ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                std::string account_text (request.get<std::string> ("account"));
//                logos::account account;
//                auto error (account.decode_account (account_text));
//                if (!error)
//                {
//                    logos::transaction transaction (node.store.environment, nullptr, false);
//                    auto account_check (existing->second->store.find (transaction, account));
//                    if (account_check != existing->second->store.end ())
//                    {
//                        std::string hash_text (request.get<std::string> ("block"));
//                        logos::uint256_union hash;
//                        auto error (hash.decode_hex (hash_text));
//                        if (!error)
//                        {
//                            auto block (node.store.block_get (transaction, hash));
//                            if (block != nullptr)
//                            {
//                                if (node.store.pending_exists (transaction, logos::pending_key (account, hash)))
//                                {
//                                    uint64_t work (0);
//                                    boost::optional<std::string> work_text (request.get_optional<std::string> ("work"));
//                                    auto error (false);
//                                    if (work_text.is_initialized ())
//                                    {
//                                        error = logos::from_string_hex (work_text.get (), work);
//                                        if (error)
//                                        {
//                                            error_response (response, "Bad work");
//                                        }
//                                    }
//                                    if (work)
//                                    {
//                                        logos::account_info info;
//                                        logos::uint256_union head;
//                                        if (!node.store.account_get (transaction, account, info))
//                                        {
//                                            head = info.head;
//                                        }
//                                        else
//                                        {
//                                            head = account;
//                                        }
//                                        if (!logos::work_validate (head, work))
//                                        {
//                                            logos::transaction transaction_a (node.store.environment, nullptr, true);
//                                            existing->second->store.work_put (transaction_a, account, work);
//                                        }
//                                        else
//                                        {
//                                            error = true;
//                                            error_response (response, "Invalid work");
//                                        }
//                                    }
//                                    if (!error)
//                                    {
//                                        auto response_a (response);
//                                        existing->second->receive_async (std::move (block), account, logos::genesis_amount, [response_a](std::shared_ptr<logos::block> block_a) {
//                                            logos::uint256_union hash_a (0);
//                                            if (block_a != nullptr)
//                                            {
//                                                hash_a = block_a->hash ();
//                                            }
//                                            boost::property_tree::ptree response_l;
//                                            response_l.put ("block", hash_a.to_string ());
//                                            response_a (response_l);
//                                        },
//                                        work == 0);
//                                    }
//                                }
//                                else
//                                {
//                                    error_response (response, "Block is not available to receive");
//                                }
//                            }
//                            else
//                            {
//                                error_response (response, "Block not found");
//                            }
//                        }
//                        else
//                        {
//                            error_response (response, "Bad block number");
//                        }
//                    }
//                    else
//                    {
//                        error_response (response, "Account not found in wallet");
//                    }
//                }
//                else
//                {
//                    error_response (response, "Bad account number");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::receive_minimum ()
{
    boost::property_tree::ptree response_l;
    response_l.put ("amount", node.config.receive_minimum.to_string_dec ());
    response (response_l);
}

void logos::rpc_handler::receive_minimum_set ()
{
    std::string amount_text (request.get<std::string> ("amount"));
    logos::uint128_union amount;
    if (!amount.decode_dec (amount_text))
    {
        node.config.receive_minimum = amount;
        boost::property_tree::ptree response_l;
        response_l.put ("success", "");
        response (response_l);
    }
    else
    {
        error_response (response, "Bad amount number");
    }
}


void logos::rpc_handler::candidates ()
{
    boost::property_tree::ptree res;

    logos::transaction txn(node.store.environment,nullptr,false);

    for(auto it = logos::store_iterator(txn,node.store.candidacy_db);
           it != logos::store_iterator(nullptr); ++it)
    {
        boost::property_tree::ptree candidate;
        bool error = false;
        CandidateInfo info(error, it->second);
        if(error)
        {
            error_response(response,"error reading candidate");
            return;
        }
        res.add_child(it->first.uint256().to_string(),info.SerializeJson());
    } 
    response(res);
}

void logos::rpc_handler::representatives ()
{
    boost::property_tree::ptree res;

    logos::transaction txn(node.store.environment,nullptr,false);

    for(auto it = logos::store_iterator(txn,node.store.representative_db);
           it != logos::store_iterator(nullptr); ++it)
    {
        boost::property_tree::ptree candidate;
        bool error = false;
        RepInfo info(error, it->second);
        if(error)
        {
            error_response(response,"error reading representative");
            return;
        }
        res.add_child(it->first.uint256().to_string(),info.SerializeJson());
    } 
    response(res);
}

void logos::rpc_handler::representatives_online ()
{
    /*boost::property_tree::ptree response_l;
    boost::property_tree::ptree representatives;
    auto reps (node.online_reps.list ());
    for (auto & i : reps)
    {
        representatives.put (i.to_account (), "");
    }
    response_l.add_child ("representatives", representatives);
    response (response_l);*/

    //CH we might need to return online reps, but it will be different.
}

void logos::rpc_handler::search_pending ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                auto error (existing->second->search_pending ());
//                boost::property_tree::ptree response_l;
//                response_l.put ("started", !error);
//                response (response_l);
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::search_pending_all ()
{
//    if (rpc.config.enable_control)
//    {
//        node.wallets.search_pending_all ();
//        boost::property_tree::ptree response_l;
//        response_l.put ("success", "");
//        response (response_l);
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::send ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                std::string source_text (request.get<std::string> ("source"));
//                logos::account source;
//                auto error (source.decode_account (source_text));
//                if (!error)
//                {
//                    std::string destination_text (request.get<std::string> ("destination"));
//                    logos::account destination;
//                    auto error (destination.decode_account (destination_text));
//                    if (!error)
//                    {
//                        std::string amount_text (request.get<std::string> ("amount"));
//                        logos::amount amount;
//                        auto error (amount.decode_dec (amount_text));
//                        if (!error)
//                        {
//                            uint64_t work (0);
//                            boost::optional<std::string> work_text (request.get_optional<std::string> ("work"));
//                            if (work_text.is_initialized ())
//                            {
//                                error = logos::from_string_hex (work_text.get (), work);
//                                if (error)
//                                {
//                                    error_response (response, "Bad work");
//                                }
//                            }
//                            logos::uint128_t balance (0);
//                            if (!error)
//                            {
//                                logos::transaction transaction (node.store.environment, nullptr, work != 0); // false if no "work" in request, true if work > 0
//                                logos::account_info info;
//                                if (!node.store.account_get (transaction, source, info))
//                                {
//                                    balance = (info.GetBalance()).number ();
//                                }
//                                else
//                                {
//                                    error = true;
//                                    error_response (response, "Account not found");
//                                }
//                                if (!error && work)
//                                {
//                                    if (!logos::work_validate (info.head, work))
//                                    {
//                                        existing->second->store.work_put (transaction, source, work);
//                                    }
//                                    else
//                                    {
//                                        error = true;
//                                        error_response (response, "Invalid work");
//                                    }
//                                }
//                            }
//                            if (!error)
//                            {
//                                boost::optional<std::string> send_id (request.get_optional<std::string> ("id"));
//                                if (balance >= amount.number ())
//                                {
//                                    auto rpc_l (shared_from_this ());
//                                    auto response_a (response);
//                                    existing->second->send_async (source, destination, amount.number (), [response_a](std::shared_ptr<logos::block> block_a) {
//                                        if (block_a != nullptr)
//                                        {
//                                            logos::uint256_union hash (block_a->hash ());
//                                            boost::property_tree::ptree response_l;
//                                            response_l.put ("block", hash.to_string ());
//                                            response_a (response_l);
//                                        }
//                                        else
//                                        {
//                                            error_response (response_a, "Error generating block");
//                                        }
//                                    },
//                                    work == 0, send_id);
//                                }
//                                else
//                                {
//                                    error_response (response, "Insufficient balance");
//                                }
//                            }
//                        }
//                        else
//                        {
//                            error_response (response, "Bad amount format");
//                        }
//                    }
//                    else
//                    {
//                        error_response (response, "Bad destination account");
//                    }
//                }
//                else
//                {
//                    error_response (response, "Bad source account");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::stats ()
{
    bool error = false;
    auto sink = node.stats.log_sink_json ();
    std::string type (request.get<std::string> ("type", ""));
    if (type == "counters")
    {
        node.stats.log_counters (*sink);
    }
    else if (type == "samples")
    {
        node.stats.log_samples (*sink);
    }
    else
    {
        error = true;
        error_response (response, "Invalid or missing type argument");
    }

    if (!error)
    {
        response (*static_cast<boost::property_tree::ptree *> (sink->to_object ()));
    }
}

void logos::rpc_handler::stop ()
{
    boost::property_tree::ptree response_l;
    response_l.put ("success", "");
    response (response_l);
    rpc.stop ();
    node.stop ();
}

void logos::rpc_handler::tokens_info ()
{
    auto res = tokens_info(request,node.store);
    if(!res.error)
    {
        response(res.contents);
    }
    else
    {
        error_response(response,res.error_msg);
    }
}

void logos::rpc_handler::txacceptor_update (bool add)
{
    std::string sepoch = request.get<std::string>("epoch");
    QueriedEpoch queried_epoch = QueriedEpoch::Next;
    if (sepoch == "current")
    {
        queried_epoch = QueriedEpoch::Current;
    }
    else if (sepoch != "next")
    {
        error_response(response, "Invalid epoch");
    }
    std::string ip = request.get<std::string>("ip");
    uint16_t port = request.get<uint16_t>("port");
    uint16_t bin_port = request.get<uint16_t>("bin_port");
    uint16_t json_port = request.get<uint16_t>("json_port");
    bool res = node._identity_manager->OnTxAcceptorUpdate(queried_epoch, ip, port, bin_port, json_port, add);
    boost::property_tree::ptree response_l;
    response_l.put ("result", res?"processing":"failed");
    response (response_l);
}

void logos::rpc_handler::txacceptor_add ()
{
    txacceptor_update(true);
}

void logos::rpc_handler::txacceptor_delete ()
{
    txacceptor_update(false);
}

void logos::rpc_handler::unchecked ()
{
//    uint64_t count (std::numeric_limits<uint64_t>::max ());
//    boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
//    if (count_text.is_initialized ())
//    {
//        auto error (decode_unsigned (count_text.get (), count));
//        if (error)
//        {
//            error_response (response, "Invalid count limit");
//        }
//    }
//    boost::property_tree::ptree response_l;
//    boost::property_tree::ptree unchecked;
//    logos::transaction transaction (node.store.environment, nullptr, false);
//    for (auto i (node.store.unchecked_begin (transaction)), n (node.store.unchecked_end ()); i != n && unchecked.size () < count; ++i)
//    {
//        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
//        auto block (logos::deserialize_block (stream));
//        std::string contents;
//        block->serialize_json (contents);
//        unchecked.put (block->hash ().to_string (), contents);
//    }
//    response_l.add_child ("blocks", unchecked);
//    response (response_l);
}

void logos::rpc_handler::unchecked_clear ()
{
//    if (rpc.config.enable_control)
//    {
//        logos::transaction transaction (node.store.environment, nullptr, true);
//        node.store.unchecked_clear (transaction);
//        boost::property_tree::ptree response_l;
//        response_l.put ("success", "");
//        response (response_l);
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::unchecked_get ()
{
//    std::string hash_text (request.get<std::string> ("hash"));
//    logos::uint256_union hash;
//    auto error (hash.decode_hex (hash_text));
//    if (!error)
//    {
//        boost::property_tree::ptree response_l;
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        for (auto i (node.store.unchecked_begin (transaction)), n (node.store.unchecked_end ()); i != n; ++i)
//        {
//            logos::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
//            auto block (logos::deserialize_block (stream));
//            if (block->hash () == hash)
//            {
//                std::string contents;
//                block->serialize_json (contents);
//                response_l.put ("contents", contents);
//                break;
//            }
//        }
//        if (!response_l.empty ())
//        {
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Unchecked block not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad hash number");
//    }
}

void logos::rpc_handler::unchecked_keys ()
{
//    uint64_t count (std::numeric_limits<uint64_t>::max ());
//    logos::uint256_union key (0);
//    boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
//    if (count_text.is_initialized ())
//    {
//        auto error (decode_unsigned (count_text.get (), count));
//        if (error)
//        {
//            error_response (response, "Invalid count limit");
//        }
//    }
//    boost::optional<std::string> hash_text (request.get_optional<std::string> ("key"));
//    if (hash_text.is_initialized ())
//    {
//        auto error_hash (key.decode_hex (hash_text.get ()));
//        if (error_hash)
//        {
//            error_response (response, "Bad key hash number");
//        }
//    }
//    boost::property_tree::ptree response_l;
//    boost::property_tree::ptree unchecked;
//    logos::transaction transaction (node.store.environment, nullptr, false);
//    for (auto i (node.store.unchecked_begin (transaction, key)), n (node.store.unchecked_end ()); i != n && unchecked.size () < count; ++i)
//    {
//        boost::property_tree::ptree entry;
//        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
//        auto block (logos::deserialize_block (stream));
//        std::string contents;
//        block->serialize_json (contents);
//        entry.put ("key", logos::block_hash (i->first.uint256 ()).to_string ());
//        entry.put ("hash", block->hash ().to_string ());
//        entry.put ("contents", contents);
//        unchecked.push_back (std::make_pair ("", entry));
//    }
//    response_l.add_child ("unchecked", unchecked);
//    response (response_l);
}

void logos::rpc_handler::version ()
{
    boost::property_tree::ptree response_l;
    response_l.put ("rpc_version", "1");
    response_l.put ("store_version", std::to_string (node.store_version ()));
    response_l.put ("node_vendor", boost::str (boost::format ("Logos %1%.%2%") % LOGOS_VERSION_MAJOR % LOGOS_VERSION_MINOR));
    response (response_l);
}

void logos::rpc_handler::validate_account_number ()
{
    std::string account_text (request.get<std::string> ("account"));
    logos::uint256_union account;
    auto error (account.decode_account (account_text));
    boost::property_tree::ptree response_l;
    response_l.put ("valid", error ? "0" : "1");
    response (response_l);
}

void logos::rpc_handler::wallet_add ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string key_text (request.get<std::string> ("key"));
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::raw_key key;
//        auto error (key.data.decode_hex (key_text));
//        if (!error)
//        {
//            logos::uint256_union wallet;
//            auto error (wallet.decode_hex (wallet_text));
//            if (!error)
//            {
//                auto existing (node.wallets.items.find (wallet));
//                if (existing != node.wallets.items.end ())
//                {
//                    const bool generate_work = request.get<bool> ("work", true);
//                    auto pub (existing->second->insert_adhoc (key, generate_work));
//                    if (!pub.is_zero ())
//                    {
//                        boost::property_tree::ptree response_l;
//                        response_l.put ("account", pub.to_account ());
//                        response (response_l);
//                    }
//                    else
//                    {
//                        error_response (response, "Wallet locked");
//                    }
//                }
//                else
//                {
//                    error_response (response, "Wallet not found");
//                }
//            }
//            else
//            {
//                error_response (response, "Bad wallet number");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad private key");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::wallet_add_watch ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                logos::transaction transaction (node.store.environment, nullptr, true);
//                if (existing->second->store.valid_password (transaction))
//                {
//                    for (auto & accounts : request.get_child ("accounts"))
//                    {
//                        std::string account_text = accounts.second.data ();
//                        logos::uint256_union account;
//                        auto error (account.decode_account (account_text));
//                        if (!error)
//                        {
//                            existing->second->insert_watch (transaction, account);
//                        }
//                        else
//                        {
//                            error_response (response, "Bad account number");
//                        }
//                    }
//                    boost::property_tree::ptree response_l;
//                    response_l.put ("success", "");
//                    response (response_l);
//                }
//                else
//                {
//                    error_response (response, "Wallet locked");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::wallet_balance_total ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            logos::uint128_t balance (0);
//            logos::uint128_t pending (0);
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            for (auto i (existing->second->store.begin (transaction)), n (existing->second->store.end ()); i != n; ++i)
//            {
////                logos::account account (i->first.uint256 ());
////                balance = balance + node.ledger.account_balance (transaction, account);
////                pending = pending + node.ledger.account_pending (transaction, account);
//            }
//            boost::property_tree::ptree response_l;
//            response_l.put ("balance", balance.convert_to<std::string> ());
//            response_l.put ("pending", pending.convert_to<std::string> ());
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::wallet_balances ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        logos::uint128_union threshold (0);
//        boost::optional<std::string> threshold_text (request.get_optional<std::string> ("threshold"));
//        if (threshold_text.is_initialized ())
//        {
//            auto error_threshold (threshold.decode_dec (threshold_text.get ()));
//            if (error_threshold)
//            {
//                error_response (response, "Bad threshold number");
//            }
//        }
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            boost::property_tree::ptree response_l;
//            boost::property_tree::ptree balances;
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            for (auto i (existing->second->store.begin (transaction)), n (existing->second->store.end ()); i != n; ++i)
//            {
//                logos::account account (i->first.uint256 ());
//                //logos::uint128_t balance = node.ledger.account_balance (transaction, account);
//                if (threshold.is_zero ())
//                {
//                    boost::property_tree::ptree entry;
//                    //logos::uint128_t pending = node.ledger.account_pending (transaction, account);
//                    //entry.put ("balance", balance.convert_to<std::string> ());
//                    //entry.put ("pending", pending.convert_to<std::string> ());
//                    balances.push_back (std::make_pair (account.to_account (), entry));
//                }
//                else
//                {
////                    if (balance >= threshold.number ())
////                    {
////                        boost::property_tree::ptree entry;
////                        //logos::uint128_t pending = node.ledger.account_pending (transaction, account);
////                        //entry.put ("balance", balance.convert_to<std::string> ());
////                        //entry.put ("pending", pending.convert_to<std::string> ());
////                        balances.push_back (std::make_pair (account.to_account (), entry));
////                    }
//                }
//            }
//            response_l.add_child ("balances", balances);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::wallet_change_seed ()
{
//    std::string seed_text (request.get<std::string> ("seed"));
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::raw_key seed;
//    auto error (seed.data.decode_hex (seed_text));
//    if (!error)
//    {
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                logos::transaction transaction (node.store.environment, nullptr, true);
//                if (existing->second->store.valid_password (transaction))
//                {
//                    existing->second->store.seed_set (transaction, seed);
//                    boost::property_tree::ptree response_l;
//                    response_l.put ("success", "");
//                    response (response_l);
//                }
//                else
//                {
//                    error_response (response, "Wallet locked");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad seed");
//    }
}

void logos::rpc_handler::wallet_contains ()
{
//    std::string account_text (request.get<std::string> ("account"));
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union account;
//    auto error (account.decode_account (account_text));
//    if (!error)
//    {
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                logos::transaction transaction (node.store.environment, nullptr, false);
//                auto exists (existing->second->store.find (transaction, account) != existing->second->store.end ());
//                boost::property_tree::ptree response_l;
//                response_l.put ("exists", exists ? "1" : "0");
//                response (response_l);
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad account number");
//    }
}

void logos::rpc_handler::wallet_create ()
{
//    if (rpc.config.enable_control)
//    {
//        logos::keypair wallet_id;
//        node.wallets.create (wallet_id.pub);
//        logos::transaction transaction (node.store.environment, nullptr, false);
//        auto existing (node.wallets.items.find (wallet_id.pub));
//        if (existing != node.wallets.items.end ())
//        {
//            boost::property_tree::ptree response_l;
//            response_l.put ("wallet", wallet_id.pub.to_string ());
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Failed to create wallet. Increase lmdb_max_dbs in node config.");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::wallet_destroy ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                node.wallets.destroy (wallet);
//                boost::property_tree::ptree response_l;
//                response (response_l);
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::wallet_export ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            std::string json;
//            existing->second->store.serialize_json (transaction, json);
//            boost::property_tree::ptree response_l;
//            response_l.put ("json", json);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad account number");
//    }
}

void logos::rpc_handler::wallet_frontiers ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            boost::property_tree::ptree response_l;
//            boost::property_tree::ptree frontiers;
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            for (auto i (existing->second->store.begin (transaction)), n (existing->second->store.end ()); i != n; ++i)
//            {
//                logos::account account (i->first.uint256 ());
////                auto latest (node.ledger.latest (transaction, account));
////                if (!latest.is_zero ())
////                {
////                    frontiers.put (account.to_account (), latest.to_string ());
////                }
//            }
//            response_l.add_child ("frontiers", frontiers);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::wallet_key_valid ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            auto valid (existing->second->store.valid_password (transaction));
//            boost::property_tree::ptree response_l;
//            response_l.put ("valid", valid ? "1" : "0");
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::wallet_ledger ()
{
//    const bool representative = request.get<bool> ("representative", false);
//    const bool weight = request.get<bool> ("weight", false);
//    const bool pending = request.get<bool> ("pending", false);
//    uint64_t modified_since (0);
//    boost::optional<std::string> modified_since_text (request.get_optional<std::string> ("modified_since"));
//    if (modified_since_text.is_initialized ())
//    {
//        modified_since = strtoul (modified_since_text.get ().c_str (), NULL, 10);
//    }
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            boost::property_tree::ptree response_l;
//            boost::property_tree::ptree accounts;
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            for (auto i (existing->second->store.begin (transaction)), n (existing->second->store.end ()); i != n; ++i)
//            {
//                logos::account account (i->first.uint256 ());
//                logos::account_info info;
//                if (!node.store.account_get (transaction, account, info))
//                {
//                    if (info.modified >= modified_since)
//                    {
//                        boost::property_tree::ptree entry;
//                        entry.put ("frontier", info.head.to_string ());
//                        entry.put ("open_block", info.open_block.to_string ());
//                        entry.put ("representative_block", info.governance_subchain_head.to_string ());
//                        std::string balance;
//                        logos::uint128_union (info.GetBalance()).encode_dec (balance);
//                        entry.put ("balance", balance);
//                        entry.put ("modified_timestamp", std::to_string (info.modified));
//                        entry.put ("request_count", std::to_string (info.block_count));
//                        if (representative)
//                        {
//                            auto block (node.store.block_get (transaction, info.governance_subchain_head));
//                            assert (block != nullptr);
//                            entry.put ("representative", block->representative ().to_account ());
//                        }
//                        if (weight)
//                        {
////                            auto account_weight (node.ledger.weight (transaction, account));
////                            entry.put ("weight", account_weight.convert_to<std::string> ());
//                        }
//                        if (pending)
//                        {
////                            auto account_pending (node.ledger.account_pending (transaction, account));
////                            entry.put ("pending", account_pending.convert_to<std::string> ());
//                        }
//                        accounts.push_back (std::make_pair (account.to_account (), entry));
//                    }
//                }
//            }
//            response_l.add_child ("accounts", accounts);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::wallet_lock ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                boost::property_tree::ptree response_l;
//                logos::raw_key empty;
//                empty.data.clear ();
//                existing->second->store.password.value_set (empty);
//                response_l.put ("locked", "1");
//                response (response_l);
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::wallet_pending ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            uint64_t count (std::numeric_limits<uint64_t>::max ());
//            logos::uint128_union threshold (0);
//            boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
//            if (count_text.is_initialized ())
//            {
//                auto error (decode_unsigned (count_text.get (), count));
//                if (error)
//                {
//                    error_response (response, "Invalid count limit");
//                }
//            }
//            boost::optional<std::string> threshold_text (request.get_optional<std::string> ("threshold"));
//            if (threshold_text.is_initialized ())
//            {
//                auto error_threshold (threshold.decode_dec (threshold_text.get ()));
//                if (error_threshold)
//                {
//                    error_response (response, "Bad threshold number");
//                }
//            }
//            const bool source = request.get<bool> ("source", false);
//            boost::property_tree::ptree response_l;
//            boost::property_tree::ptree pending;
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            for (auto i (existing->second->store.begin (transaction)), n (existing->second->store.end ()); i != n; ++i)
//            {
//                logos::account account (i->first.uint256 ());
//                boost::property_tree::ptree peers_l;
//                logos::account end (account.number () + 1);
//                for (auto ii (node.store.pending_begin (transaction, logos::pending_key (account, 0))), nn (node.store.pending_begin (transaction, logos::pending_key (end, 0))); ii != nn && peers_l.size () < count; ++ii)
//                {
//                    logos::pending_key key (ii->first);
//                    if (threshold.is_zero () && !source)
//                    {
//                        boost::property_tree::ptree entry;
//                        entry.put ("", key.hash.to_string ());
//                        peers_l.push_back (std::make_pair ("", entry));
//                    }
//                    else
//                    {
//                        logos::pending_info info (ii->second);
//                        if (info.amount.number () >= threshold.number ())
//                        {
//                            if (source)
//                            {
//                                boost::property_tree::ptree pending_tree;
//                                pending_tree.put ("amount", info.amount.number ().convert_to<std::string> ());
//                                pending_tree.put ("source", info.source.to_account ());
//                                peers_l.add_child (key.hash.to_string (), pending_tree);
//                            }
//                            else
//                            {
//                                peers_l.put (key.hash.to_string (), info.amount.number ().convert_to<std::string> ());
//                            }
//                        }
//                    }
//                }
//                if (!peers_l.empty ())
//                {
//                    pending.add_child (account.to_account (), peers_l);
//                }
//            }
//            response_l.add_child ("blocks", pending);
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::wallet_representative ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            logos::transaction transaction (node.store.environment, nullptr, false);
//            boost::property_tree::ptree response_l;
//            response_l.put ("representative", existing->second->store.representative (transaction).to_account ());
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad account number");
//    }
}

void logos::rpc_handler::wallet_representative_set ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                std::string representative_text (request.get<std::string> ("representative"));
//                logos::account representative;
//                auto error (representative.decode_account (representative_text));
//                if (!error)
//                {
//                    logos::transaction transaction (node.store.environment, nullptr, true);
//                    existing->second->store.representative_set (transaction, representative);
//                    boost::property_tree::ptree response_l;
//                    response_l.put ("set", "1");
//                    response (response_l);
//                }
//                else
//                {
//                    error_response (response, "Invalid account number");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad account number");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}


void logos::rpc_handler::wallet_work_get ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                boost::property_tree::ptree response_l;
//                boost::property_tree::ptree works;
//                logos::transaction transaction (node.store.environment, nullptr, false);
//                for (auto i (existing->second->store.begin (transaction)), n (existing->second->store.end ()); i != n; ++i)
//                {
//                    logos::account account (i->first.uint256 ());
//                    uint64_t work (0);
//                    auto error_work (existing->second->store.work_get (transaction, account, work));
//                    works.put (account.to_account (), logos::to_string_hex (work));
//                }
//                response_l.add_child ("works", works);
//                response (response_l);
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::work_generate ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string hash_text (request.get<std::string> ("hash"));
//        bool use_peers (request.get_optional<bool> ("use_peers") == true);
//        logos::block_hash hash;
//        auto error (hash.decode_hex (hash_text));
//        if (!error)
//        {
//            auto rpc_l (shared_from_this ());
//            auto callback = [rpc_l](boost::optional<uint64_t> const & work_a) {
//                if (work_a)
//                {
//                    boost::property_tree::ptree response_l;
//                    response_l.put ("work", logos::to_string_hex (work_a.value ()));
//                    rpc_l->response (response_l);
//                }
//                else
//                {
//                    error_response (rpc_l->response, "Cancelled");
//                }
//            };
//            if (!use_peers)
//            {
//                node.work.generate (hash, callback);
//            }
//            else
//            {
//                node.work_generate (hash, callback);
//            }
//        }
//        else
//        {
//            error_response (response, "Bad block hash");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::work_cancel ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string hash_text (request.get<std::string> ("hash"));
//        logos::block_hash hash;
//        auto error (hash.decode_hex (hash_text));
//        if (!error)
//        {
//            node.work.cancel (hash);
//            boost::property_tree::ptree response_l;
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Bad block hash");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::work_get ()
{
//    if (rpc.config.enable_control)
//    {
//        std::string wallet_text (request.get<std::string> ("wallet"));
//        logos::uint256_union wallet;
//        auto error (wallet.decode_hex (wallet_text));
//        if (!error)
//        {
//            auto existing (node.wallets.items.find (wallet));
//            if (existing != node.wallets.items.end ())
//            {
//                std::string account_text (request.get<std::string> ("account"));
//                logos::account account;
//                auto error (account.decode_account (account_text));
//                if (!error)
//                {
//                    logos::transaction transaction (node.store.environment, nullptr, false);
//                    auto account_check (existing->second->store.find (transaction, account));
//                    if (account_check != existing->second->store.end ())
//                    {
//                        uint64_t work (0);
//                        auto error_work (existing->second->store.work_get (transaction, account, work));
//                        boost::property_tree::ptree response_l;
//                        response_l.put ("work", logos::to_string_hex (work));
//                        response (response_l);
//                    }
//                    else
//                    {
//                        error_response (response, "Account not found in wallet");
//                    }
//                }
//                else
//                {
//                    error_response (response, "Bad account number");
//                }
//            }
//            else
//            {
//                error_response (response, "Wallet not found");
//            }
//        }
//        else
//        {
//            error_response (response, "Bad wallet number");
//        }
//    }
//    else
//    {
//        error_response (response, "RPC control is disabled");
//    }
}

void logos::rpc_handler::work_set ()
{
//    std::string wallet_text (request.get<std::string> ("wallet"));
//    logos::uint256_union wallet;
//    auto error (wallet.decode_hex (wallet_text));
//    if (!error)
//    {
//        auto existing (node.wallets.items.find (wallet));
//        if (existing != node.wallets.items.end ())
//        {
//            std::string account_text (request.get<std::string> ("account"));
//            logos::account account;
//            auto error (account.decode_account (account_text));
//            if (!error)
//            {
//                logos::transaction transaction (node.store.environment, nullptr, true);
//                auto account_check (existing->second->store.find (transaction, account));
//                if (account_check != existing->second->store.end ())
//                {
//                    std::string work_text (request.get<std::string> ("work"));
//                    uint64_t work;
//                    auto work_error (logos::from_string_hex (work_text, work));
//                    if (!work_error)
//                    {
//                        existing->second->store.work_put (transaction, account, work);
//                        boost::property_tree::ptree response_l;
//                        response_l.put ("success", "");
//                        response (response_l);
//                    }
//                    else
//                    {
//                        error_response (response, "Bad work");
//                    }
//                }
//                else
//                {
//                    error_response (response, "Account not found in wallet");
//                }
//            }
//            else
//            {
//                error_response (response, "Bad account number");
//            }
//        }
//        else
//        {
//            error_response (response, "Wallet not found");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad wallet number");
//    }
}

void logos::rpc_handler::work_validate ()
{
//    std::string hash_text (request.get<std::string> ("hash"));
//    logos::block_hash hash;
//    auto error (hash.decode_hex (hash_text));
//    if (!error)
//    {
//        std::string work_text (request.get<std::string> ("work"));
//        uint64_t work;
//        auto work_error (logos::from_string_hex (work_text, work));
//        if (!work_error)
//        {
//            auto validate (logos::work_validate (hash, work));
//            boost::property_tree::ptree response_l;
//            response_l.put ("valid", validate ? "0" : "1");
//            response (response_l);
//        }
//        else
//        {
//            error_response (response, "Bad work");
//        }
//    }
//    else
//    {
//        error_response (response, "Bad block hash");
//    }
}

void logos::rpc_handler::work_peer_add ()
{
    std::string address_text = request.get<std::string> ("address");
    std::string port_text = request.get<std::string> ("port");
    uint16_t port;
    if (!logos::parse_port (port_text, port))
    {
        node.config.work_peers.push_back (std::make_pair (address_text, port));
        boost::property_tree::ptree response_l;
        response_l.put ("success", "");
        response (response_l);
    }
    else
    {
        error_response (response, "Invalid port");
    }
}

void logos::rpc_handler::work_peers ()
{
    boost::property_tree::ptree work_peers_l;
    for (auto i (node.config.work_peers.begin ()), n (node.config.work_peers.end ()); i != n; ++i)
    {
        boost::property_tree::ptree entry;
        entry.put ("", boost::str (boost::format ("%1%:%2%") % i->first % i->second));
        work_peers_l.push_back (std::make_pair ("", entry));
    }
    boost::property_tree::ptree response_l;
    response_l.add_child ("work_peers", work_peers_l);
    response (response_l);
}

void logos::rpc_handler::work_peers_clear ()
{
    node.config.work_peers.clear ();
    boost::property_tree::ptree response_l;
    response_l.put ("success", "");
    response (response_l);
}

void logos::rpc_handler::sleeve_unlock()
{
    using namespace request::fields;
    std::string password (request.get<std::string>(PASSWORD));
    auto status = node._identity_manager->UnlockSleeve(password);
    if (!status)
    {
        error_response (response, SleeveResultToString(status.code))
    }
    boost::property_tree::ptree resp_tree;
    resp_tree.put("success", true);
    response (resp_tree);
}

void logos::rpc_handler::sleeve_lock()
{
    auto status (node._identity_manager->LockSleeve());
    if (!status)
    {
        error_response (response, SleeveResultToString(status.code))
    }
    boost::property_tree::ptree resp_tree;
    resp_tree.put("success", true);
    response (resp_tree);
}

void logos::rpc_handler::sleeve_update_password()
{
    using namespace request::fields;
    std::string password (request.get<std::string>(PASSWORD));
    logos::transaction tx(node._sleeve._env, nullptr, true);
    auto status (node._sleeve.Rekey(password, tx));

    if (!status)
    {
        error_response (response, SleeveResultToString(status.code))
    }

    boost::property_tree::ptree resp_tree;
    resp_tree.put("success", true);
    response (resp_tree);
}

void logos::rpc_handler::sleeve_store_keys()
{
    using namespace request::fields;
    ByteArray<32> ecies_prv (request.get<std::string>(request::fields::ECIES));
    if (ecies_prv.is_zero())
    {
        error_response (response, "Bad ECIES private key");
    }
    ByteArray<32> bls_prv (request.get<std::string>(BLS));
    if (bls_prv.is_zero())
    {
        error_response (response, "Bad BLS private key");
    }
    bool overwrite (request.get<bool>(OVERWRITE, false));

    auto status (node._identity_manager->Sleeve(bls_prv, ecies_prv, overwrite));

    if (!status)
    {
        error_response (response, SleeveResultToString(status.code));
    }

    boost::property_tree::ptree resp_tree;
    resp_tree.put("success", true);
    response (resp_tree);
}

void logos::rpc_handler::unsleeve()
{
    auto status (node._identity_manager->Unsleeve());
    if (!status)
    {
        error_response (response, SleeveResultToString(status.code))
    }
    boost::property_tree::ptree resp_tree;
    resp_tree.put("success", true);
    response (resp_tree);
}

void logos::rpc_handler::sleeve_reset()
{
    node._identity_manager->ResetSleeve();
    boost::property_tree::ptree resp_tree;
    resp_tree.put("success", true);
    response (resp_tree);
}

void logos::rpc_handler::delegate_activate(bool activate)
{
    using namespace request::fields;
    uint32_t epoch_num (request.get<uint32_t> (EPOCH_NUM, 0));

    auto status (node._identity_manager->ChangeActivation(activate, epoch_num));

    if (!status)
    {
        error_response (response, SleeveResultToString(status.code))
    }
    boost::property_tree::ptree resp_tree;
    resp_tree.put("success", true);
    response (resp_tree);
}

void logos::rpc_handler::cancel_activation_scheduling()
{
    auto status (node._identity_manager->CancelActivationScheduling());

    if (!status)
    {
        error_response (response, SleeveResultToString(status.code))
    }
    boost::property_tree::ptree resp_tree;
    resp_tree.put("success", true);
    response (resp_tree);
}

void logos::rpc_handler::activation_status()
{
    boost::property_tree::ptree resp_tree;
    bool unlocked = node._sleeve.IsUnlocked();
    resp_tree.put("unlocked", unlocked);
    if (unlocked)
    {
        bool sleeved, activated;
        DelegateIdentityManager::activation_schedule schedule;
        std::tie(sleeved, activated, schedule) = node._identity_manager->GetActivationSummary();
        resp_tree.put("sleeved", sleeved);
        resp_tree.put("activated", activated);
        if (sleeved && activated)  // both sleeved and activated now
        {
            auto delegate_idx = node._consensus_container->GetCurDelegateIdx();
            if (delegate_idx != NON_DELEGATE)
            {
                resp_tree.put("delegate_idx", delegate_idx);
            }
            else
            {
                resp_tree.put("delegate_idx", "-1");
            }
        }
        if (schedule.start_epoch > ConsensusContainer::GetCurEpochNumber())
        {
            resp_tree.put("scheduled_epoch", schedule.start_epoch);
            resp_tree.put("scheduled_activate", schedule.activate);
        }
    }
    response (resp_tree);
}

void logos::rpc_handler::new_bls_key_pair()
{
    bls::KeyPair bls;

    boost::property_tree::ptree resp_tree;
    resp_tree.put("bls_prv", bls.prv.to_string());
    resp_tree.put("bls_pub", bls.pub.to_string());
    response (resp_tree);
}

void logos::rpc_handler::new_ecies_key_pair()
{
    ECIESKeyPair ecies;

    boost::property_tree::ptree resp_tree;
    resp_tree.put("ecies_prv", ecies.prv.ToHexString());
    resp_tree.put("ecies_pub", ecies.pub.ToHexString());
    response (resp_tree);
}

logos::rpc_connection::rpc_connection (logos::node & node_a, logos::rpc & rpc_a) :
node (node_a.shared ()),
rpc (rpc_a),
socket (node_a.service)
{
    responded.clear ();
}

void logos::rpc_connection::parse_connection ()
{
    read ();
}

void logos::rpc_connection::write_result (std::string body, unsigned version)
{
    if (!responded.test_and_set ())
    {
        res.set ("Content-Type", "application/json");
        res.set ("Access-Control-Allow-Origin", "*");
        res.set ("Access-Control-Allow-Headers", "Accept, Accept-Language, Content-Language, Content-Type");
        res.set ("Connection", "close");
        res.result (boost::beast::http::status::ok);
        boost::replace_all(body, "\"[]\"", "[]");
        res.body () = body;
        res.version (version);
        res.prepare_payload ();
    }
    else
    {
        assert (false && "RPC already responded and should only respond once");
        // Guards `res' from being clobbered while async_write is being serviced
    }
}

void logos::rpc_connection::read ()
{
    auto this_l (shared_from_this ());
    boost::beast::http::async_read (socket, buffer, request, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
        if (!ec)
        {
            this_l->node->background ([this_l]() {
                auto start (std::chrono::steady_clock::now ());
                auto version (this_l->request.version ());
                auto response_handler ([this_l, version, start](boost::property_tree::ptree const & tree_a) {

                    std::stringstream ostream;
                    boost::property_tree::write_json (ostream, tree_a);
                    ostream.flush ();
                    auto body (ostream.str ());
                    this_l->write_result (body, version);
                    boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
                    });

                    if (this_l->node->config.logging.log_rpc ())
                    {
                        BOOST_LOG (this_l->node->log) << boost::str (boost::format ("RPC request %2% completed in: %1% microseconds") % std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count () % boost::io::group (std::hex, std::showbase, reinterpret_cast<uintptr_t> (this_l.get ())));
                    }
                });
                if (this_l->request.method () == boost::beast::http::verb::post)
                {
                    auto handler (std::make_shared<logos::rpc_handler> (*this_l->node, this_l->rpc, this_l->request.body (), response_handler));
                    handler->process_request ();
                }
                else
                {
                    error_response (response_handler, "Can only POST requests");
                }
            });
        }
        else
        {
            BOOST_LOG (this_l->node->log) << "RPC read error: " << ec.message ();
        }
    });
}

namespace
{
void reprocess_body (std::string & body, boost::property_tree::ptree & tree_a)
{
    std::stringstream stream;
    boost::property_tree::write_json (stream, tree_a);
    body = stream.str ();
}
}

void logos::rpc_handler::process_request ()
{
    try
    {
        std::stringstream istream (body);
        boost::property_tree::read_json (istream, request);
        std::string action (request.get<std::string> ("rpc_action"));
        if (action == "password_enter")
        {
            password_enter ();
            request.erase ("password");
            reprocess_body (body, request);
        }
        else if (action == "password_change")
        {
            rpc_control([this](){password_change ();});
            request.erase ("password");
            reprocess_body (body, request);
        }
        else if (action == "wallet_unlock")
        {
            password_enter ();
            request.erase ("password");
            reprocess_body (body, request);
        }
        if (node.config.logging.log_rpc ())
        {
            LOG_INFO (node.log) << body;
        }
        if (action == "account_balance")
        {
            account_balance ();
        }
        else if (action == "account_block_count")
        {
            account_block_count ();
        }
        else if (action == "account_count")
        {
            account_count ();
        }
        else if (action == "account_create")
        {
            rpc_control([this](){account_create ();});
        }
        else if (action == "account_from_key")
        {
            account_from_key ();
        }
        else if (action == "account_history")
        {
            account_history ();
        }
        else if (action == "account_info")
        {
            account_info ();
        }
        else if (action == "account_to_key")
        {
            account_to_key ();
        }
        else if (action == "account_list")
        {
            account_list ();
        }
        else if (action == "account_move")
        {
            rpc_control([this](){account_move ();});
        }
        else if (action == "account_remove")
        {
            rpc_control([this](){account_remove ();});
        }
        else if (action == "account_representative")
        {
            account_representative ();
        }
        else if (action == "account_representative_set")
        {
            account_representative_set ();
        }
        else if (action == "account_weight")
        {
            account_weight ();
        }
        else if (action == "accounts_balances")
        {
            accounts_balances ();
        }
        else if (action == "accounts_create")
        {
            rpc_control([this](){accounts_create ();});
        }
        else if (action == "accounts_exist")
        {
            accounts_exist ();
        }
        else if (action == "accounts_frontiers")
        {
            accounts_frontiers ();
        }
        else if (action == "accounts_pending")
        {
            accounts_pending ();
        }
        else if (action == "available_supply")
        {
            available_supply ();
        }
        else if (action == "request_blocks")
        {
            request_blocks ();
        }
        else if (action == "request_blocks_latest")
        {
            request_blocks_latest ();
        }
        else if (action == "block")
        {
            block ();
        }
        else if (action == "block_confirm")
        {
            //block_confirm ();
        }
        else if (action == "blocks")
        {
            blocks ();
        }
        else if (action == "blocks_exist")
        {
            blocks_exist ();
        }
        else if (action == "block_account")
        {
            block_account ();
        }
        else if (action == "block_create")
        {
            rpc_control([this](){block_create ();});
        }
        else if (action == "block_hash")
        {
            block_hash ();
        }
        else if (action == "successors")
        {
            successors ();
        }
        else if (action == "bootstrap")
        {
            bootstrap ();
        }
        else if (action == "bootstrap_any")
        {
            bootstrap_any ();
        }
        else if (action == "bootstrap_progress")
        {
            bootstrap_progress ();
        }
        else if (action == "chain")
        {
            chain ();
        }
        else if (action == "candidates")
        {
            candidates();
        }
        else if (action == "delegators")
        {
            delegators ();
        }
        else if (action == "delegators_count")
        {
            delegators_count ();
        }
        else if (action == "deterministic_key")
        {
            deterministic_key ();
        }
        else if (action == "confirmation_history")
        {
            //CH confirmation_history ();
        }
        else if (action == "epochs")
        {
            epochs ();
        }
        else if (action == "epochs_latest")
        {
            epochs_latest ();
        }
        else if (action == "frontiers")
        {
            frontiers ();
        }
        else if (action == "frontier_count")
        {
            account_count ();
        }
        else if (action == "history")
        {
            request.put ("head", request.get<std::string> ("hash"));
            account_history ();
        }
        else if (action == "key_create")
        {
            key_create ();
        }
        else if (action == "key_expand")
        {
            key_expand ();
        }
        else if (action == "krai_from_raw")
        {
            krai_from_raw ();
        }
        else if (action == "krai_to_raw")
        {
            krai_to_raw ();
        }
        else if (action == "ledger")
        {
            ledger ();
        }
        else if (action == "micro_blocks")
        {
            micro_blocks ();
        }
        else if (action == "micro_blocks_latest")
        {
            micro_blocks_latest ();
        }
        else if (action == "mrai_from_raw")
        {
            mrai_from_raw ();
        }
        else if (action == "mrai_to_raw")
        {
            mrai_to_raw ();
        }
        else if (action == "password_change")
        {
            // Processed before logging
        }
        else if (action == "password_enter")
        {
            // Processed before logging
        }
        else if (action == "password_valid")
        {
            password_valid ();
        }
        else if (action == "payment_begin")
        {
            payment_begin ();
        }
        else if (action == "payment_init")
        {
            payment_init ();
        }
        else if (action == "payment_end")
        {
            payment_end ();
        }
        else if (action == "payment_wait")
        {
            payment_wait ();
        }
        else if (action == "peers")
        {
            //peers ();
        }
        else if (action == "pending")
        {
            pending ();
        }
        else if (action == "pending_exists")
        {
            pending_exists ();
        }
        else if (action == "process")
        {
            process ();
        }
        else if (action == "rai_from_raw")
        {
            rai_from_raw ();
        }
        else if (action == "rai_to_raw")
        {
            rai_to_raw ();
        }
//        else if (action == "receive")
//        {
//            receive ();
//        }
        else if (action == "receive_minimum")
        {
            rpc_control([this](){receive_minimum ();});
        }
        else if (action == "receive_minimum_set")
        {
            rpc_control([this](){receive_minimum_set ();});
        }
        else if (action == "representatives")
        {
            representatives ();
        }
        else if (action == "representatives_online")
        {
            representatives_online ();
        }
        else if (action == "republish")
        {
            //CH
            //republish ();
        }
        else if (action == "search_pending")
        {
            search_pending ();
        }
        else if (action == "search_pending_all")
        {
            search_pending_all ();
        }
        else if (action == "send")
        {
            send ();
        }
        else if (action == "stats")
        {
            stats ();
        }
        else if (action == "stop")
        {
            rpc_control([this](){stop ();});
        }
        else if(action == "tokens_info")
        {
            tokens_info();
        }
        else if(action == "txacceptor_add")
        {
            rpc_control([this](){txacceptor_add();});
        }
        else if(action == "txacceptor_delete")
        {
            rpc_control([this](){txacceptor_delete();});
        }
        else if (action == "unchecked")
        {
            unchecked ();
        }
        else if (action == "unchecked_clear")
        {
            unchecked_clear ();
        }
        else if (action == "unchecked_get")
        {
            unchecked_get ();
        }
        else if (action == "unchecked_keys")
        {
            unchecked_keys ();
        }
        else if (action == "validate_account_number")
        {
            validate_account_number ();
        }
        else if (action == "version")
        {
            version ();
        }
        else if (action == "wallet_add")
        {
            wallet_add ();
        }
        else if (action == "wallet_add_watch")
        {
            wallet_add_watch ();
        }
        else if (action == "wallet_balance_total")
        {
            wallet_balance_total ();
        }
        else if (action == "wallet_balances")
        {
            wallet_balances ();
        }
        else if (action == "wallet_change_seed")
        {
            rpc_control([this](){wallet_change_seed ();});
        }
        else if (action == "wallet_contains")
        {
            wallet_contains ();
        }
        else if (action == "wallet_create")
        {
            wallet_create ();
        }
        else if (action == "wallet_destroy")
        {
            wallet_destroy ();
        }
        else if (action == "wallet_export")
        {
            wallet_export ();
        }
        else if (action == "wallet_frontiers")
        {
            wallet_frontiers ();
        }
        else if (action == "wallet_key_valid")
        {
            wallet_key_valid ();
        }
        else if (action == "wallet_ledger")
        {
            wallet_ledger ();
        }
        else if (action == "wallet_lock")
        {
            wallet_lock ();
        }
        else if (action == "wallet_locked")
        {
            password_valid (true);
        }
        else if (action == "wallet_pending")
        {
            wallet_pending ();
        }
        else if (action == "wallet_representative")
        {
            wallet_representative ();
        }
        else if (action == "wallet_representative_set")
        {
            wallet_representative_set ();
        }
        else if (action == "wallet_republish")
        {
            //CH
            //wallet_republish ();
        }
        else if (action == "wallet_unlock")
        {
            // Processed before logging
        }
        else if (action == "wallet_work_get")
        {
            wallet_work_get ();
        }
        else if (action == "work_generate")
        {
            work_generate ();
        }
        else if (action == "work_cancel")
        {
            work_cancel ();
        }
        else if (action == "work_get")
        {
            work_get ();
        }
        else if (action == "work_set")
        {
            rpc_control([this](){work_set ();});
        }
        else if (action == "work_validate")
        {
            work_validate ();
        }
        else if (action == "work_peer_add")
        {
            rpc_control([this](){work_peer_add ();});
        }
        else if (action == "work_peers")
        {
            rpc_control([this](){work_peers ();});
        }
        else if (action == "work_peers_clear")
        {
            rpc_control([this](){work_peers_clear ();});
        }
        else if (action == "buffer_complete")
        {
            buffer_complete ();
        }
        else if (action == "sleeve_unlock")
        {
            identity_control([this](){
                sleeve_unlock();
                using namespace request::fields;
                request.erase (PASSWORD);
                reprocess_body (body, request);
            });
        }
        else if (action == "sleeve_lock")
        {
            identity_control([this](){sleeve_lock();});
        }
        else if (action == "sleeve_update_password")
        {
            identity_control([this](){
                sleeve_update_password();
                using namespace request::fields;
                request.erase (PASSWORD);
                reprocess_body (body, request);
            });
        }
        else if (action == "sleeve_store_keys")
        {
            identity_control([this](){
                sleeve_store_keys();
                using namespace request::fields;
                request.erase (request::fields::ECIES);
                request.erase (BLS);
                reprocess_body (body, request);
            });
        }
        else if (action == "unsleeve")
        {
            identity_control([this](){unsleeve();});
        }
        else if (action == "sleeve_reset")
        {
            identity_control([this](){sleeve_reset();});
        }
        else if (action == "delegate_activate")
        {
            identity_control([this](){delegate_activate(true);});
        }
        else if (action == "delegate_deactivate")
        {
            identity_control([this](){delegate_activate(false);});
        }
        else if (action == "cancel_activation_scheduling")
        {
            identity_control([this](){cancel_activation_scheduling();});
        }
        else if (action == "activation_status")
        {
            activation_status();
        }
        else if (action == "new_bls_key_pair")
        {
            new_bls_key_pair();
        }
        else if (action == "new_ecies_key_pair")
        {
            new_ecies_key_pair();
        }
        else if (MicroBlockTester::microblock_tester(action, request, response, node))
        {
            return;
        }
        else
        {
            error_response (response, "Unknown command");
        }
    }
    catch (std::runtime_error const & err)
    {
//        error_response (response, "Unable to parse JSON");
        error_response (response, err.what());
    }
    catch (...)
    {
        error_response (response, "Internal server error in RPC");
    }
}


void logos::rpc_handler::buffer_complete()
{
    auto result = node.BufferComplete();

    if(result.code == process_result::buffering_done)
    {
        boost::property_tree::ptree response_l;
        response_l.put ("result", "Buffering Done");
        response (response_l);
    }
    else
    {
        error_response(response, "Signaling buffer completion failed.");
    }
}

bool logos::rpc_handler::is_logos_request()
{
    return flag_present("logos");
}

bool logos::rpc_handler::should_buffer_request()
{
    return flag_present("buffer");
}

bool logos::rpc_handler::flag_present(const std::string & flag_name)
{
    boost::optional<std::string> flag(request.get_optional<std::string>(flag_name));
    return flag.is_initialized();
}

logos::rpc_handler::RpcResponse<boost::property_tree::ptree>
logos::rpc_handler::tokens_info(
        const boost::property_tree::ptree& request,
        logos::block_store& store)
{
    Log log;
    RpcResponse<boost::property_tree::ptree> res;
    res.error = false;
    try{
        boost::property_tree::ptree response;
        bool details = false;
        boost::optional<std::string> details_text (
                request.get_optional<std::string>("details"));
        if(details_text.is_initialized())
        {
            details = details_text.get() == "true";
        }

        for(auto & item : request.get_child("tokens"))
        {
            std::string account_string(item.second.get_value<std::string>());
            logos::uint256_union account(account_string);
            TokenAccount token_account_info;
            if(!store.token_account_get(account,token_account_info))
            {
                LOG_INFO(log) << "rpclogoc::tokens_info - serializing "
                << "token account to json for account : " << account_string;
                response.add_child(
                        account_string,
                        token_account_info.SerializeJson(details));
            } else
            {
                res.error = true;
            }
        }
        res.contents = response;
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

logos::rpc_handler::RpcResponse<boost::property_tree::ptree>
logos::rpc_handler::account_info(
        const boost::property_tree::ptree& request,
        logos::block_store& store)
{
    RpcResponse<boost::property_tree::ptree> res;
    res.error = false;
    try {
        std::string account_text (request.get<std::string> ("account"));
        logos::uint256_union account;
        res.error = account.decode_account (account_text);
        if (!res.error)
        {
            const bool representative =
                request.get<bool>("representative", false);
            const bool weight = request.get<bool>("weight", false);
            logos::transaction transaction (store.environment, nullptr, false);
            std::shared_ptr<logos::Account> account_ptr;
            if(store.account_get(account,account_ptr))
            {
                res.error = true;
                res.error_msg = "failed to get account";
                return res;
            }

            boost::property_tree::ptree response;

            if(account_ptr->type == logos::AccountType::TokenAccount)
            {
                TokenAccount token_account =
                    *static_pointer_cast<TokenAccount>(account_ptr);
                response = token_account.SerializeJson(true);
                response.put("type","TokenAccount");
                response.put("sequence",token_account.block_count);
                response.put("request_count",
                        std::to_string(token_account.block_count + token_account.receive_count));
                response.put("frontier",token_account.head.to_string());
                response.put("receive_tip",token_account.receive_head.to_string());
                std::string balance;
                logos::uint128_union (token_account.GetBalance()).encode_dec (balance);
                response.put ("balance", balance);
                res.contents = response;
            }
            else
            {
                logos::account_info info =
                    *static_pointer_cast<logos::account_info>(account_ptr);

                res.error = store.account_get (account, info, transaction);
                if (!res.error)
                {
                    response.put("type","LogosAccount");
                    response.put ("frontier", info.head.to_string ());
                    response.put ("receive_tip", info.receive_head.to_string ());
                    response.put ("open_block", info.open_block.to_string ());
                    response.put ("representative_block",
                            info.governance_subchain_head.to_string ());
                    std::string balance;
                    logos::uint128_union (info.GetBalance()).encode_dec (balance);
                    response.put ("balance", balance);
                    response.put ("modified_timestamp",
                            std::to_string (info.modified));
                    response.put ("request_count",
                            std::to_string(info.block_count + info.receive_count));
                    response.put("sequence",info.block_count);
                    response.put("governance_subchain_head",info.governance_subchain_head.to_string());

                    std::unordered_set<std::string> token_ids;
                    boost::property_tree::ptree token_tree;
                    bool nofilter = true;
                    if(request.find("tokens")!=request.not_found())
                    {
                        nofilter = false;
                        for(auto& t : request.get_child("tokens"))
                        {
                            token_ids.emplace(t.second.data());
                        }

                    }
                    for(TokenEntry& e : info.entries)
                    {
                        auto token_id_str = e.token_id.to_string();
                        if(nofilter ||
                                token_ids.find(e.token_id.to_string())
                                !=token_ids.end())
                        {
                            boost::property_tree::ptree entry_tree;
                            entry_tree.put("whitelisted",e.status.whitelisted);
                            entry_tree.put("frozen",e.status.frozen);
                            entry_tree.put("balance",e.balance.to_string_dec());
                            token_tree.add_child(e.token_id.to_string(),entry_tree);
                        }
                    }
                    if(token_tree.size() > 0)
                    {
                        response.add_child("tokens",token_tree);
                    }

                    res.contents = response;
                }
                else
                {
                    res.error_msg = "Account not found";
                }
            }
        }
        else
        {
            res.error_msg = "Bad account number";
        }
    } catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

logos::rpc_handler::RpcResponse<boost::property_tree::ptree>
logos::rpc_handler::account_balance(
        const boost::property_tree::ptree& request,
        logos::block_store& store)
{

    RpcResponse<boost::property_tree::ptree> res;
    res.error = false;
    try {
        boost::property_tree::ptree response;

        std::string account_text (request.get<std::string> ("account"));
        logos::uint256_union account;
        res.error = account.decode_account(account_text);
        if(res.error)
        {
            res.error_msg = "failed to decode account: " + account_text;
            return res;
        }

        logos::transaction txn (store.environment, nullptr, false);
        logos::account_info account_info;
        res.error = store.account_get(account, account_info,txn);
        if(res.error)
        {
            res.error_msg = "failed to get account from db: "
                + account.to_string();
        }
        std::string balance_str;
        account_info.GetBalance().encode_dec(balance_str);
        response.put("balance",balance_str);

        std::unordered_set<std::string> token_ids;
        boost::property_tree::ptree token_tree;
        bool nofilter = true;
        if(request.find("tokens")!=request.not_found())
        {
            nofilter = false;
            for(auto& t : request.get_child("tokens"))
            {
                token_ids.emplace(t.second.data());
            }

        }
        for(TokenEntry& e : account_info.entries)
        {
            if(nofilter ||
                    token_ids.find(e.token_id.to_string())!=token_ids.end())
            {
                token_tree.put(e.token_id.to_string(),e.balance.to_string_dec());
            }
        }
        if(token_tree.size() > 0)
        {
            response.add_child("token_balances",token_tree);
        }

        res.contents = response;
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

logos::rpc_handler::RpcResponse<boost::property_tree::ptree>
logos::rpc_handler::accounts_exist(
        const boost::property_tree::ptree& request,
        logos::block_store& store)
{
    RpcResponse<boost::property_tree::ptree> res;
    res.error = false;
    try
    {
        std::vector<std::string> hashes;
        bool exist (true);
        logos::transaction transaction (store.environment, nullptr, false);
        for (const boost::property_tree::ptree::value_type & accounts : request.get_child ("accounts"))
        {
            std::string account_text = accounts.second.data ();
            logos::account account;
            if (account.decode_account (account_text))
            {
                res.error = true;
                res.error_msg += "Bad account number: " + account_text + " ";
                break;
            }
            else
            {
                if (!store.account_exists(account))
                {
                    exist = false;
                    break;
                }
            }
        }
        res.contents.put ("exist", exist ? "1" : "0");
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

boost::property_tree::ptree getBlockJson(
        const logos::uint256_union& hash,
        logos::block_store& store)
{
    logos::transaction transaction (store.environment, nullptr, false);
    std::shared_ptr<Request> request_ptr;
    ReceiveBlock receive;
    if (!store.request_get(hash, request_ptr, transaction))
    {
        return request_ptr->SerializeJson();
    }
    else if (!store.receive_get(hash, receive, transaction))
    {
        return receive.SerializeJson();
    }
    else
    {
        boost::property_tree::ptree empty_tree;
        return empty_tree;
    }
}

logos::rpc_handler::RpcResponse<boost::property_tree::ptree>
logos::rpc_handler::block(
        const boost::property_tree::ptree& request,
        logos::block_store& store)
{
    RpcResponse<boost::property_tree::ptree> res;
    res.error = false;
    try
    {
        std::string hash_text (request.get<std::string> ("hash"));
        logos::uint256_union hash;
        if (hash.decode_hex (hash_text))
        {
            res.error = true;
            res.error_msg = "Bad hash number";
            return res;
        }
        res.contents = getBlockJson(hash,store);
        if(res.contents.empty())
        {
            res.error = true;
            res.error_msg = "block not found: " + hash_text;
        }
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

logos::rpc_handler::RpcResponse<boost::property_tree::ptree>
logos::rpc_handler::blocks(
        const boost::property_tree::ptree& request,
        logos::block_store& store)
{
    RpcResponse<boost::property_tree::ptree> res;
    res.error = false;
    try
    {
        std::vector<std::string> hashes;
        boost::property_tree::ptree blocks;
        logos::transaction transaction (store.environment, nullptr, false);
        for (const boost::property_tree::ptree::value_type & hashes : request.get_child ("hashes"))
        {
            std::string hash_text = hashes.second.data ();
            logos::uint256_union hash;
            if (hash.decode_hex (hash_text))
            {
                res.error = true;
                res.error_msg += "Bad hash number: " + hash_text + " .";
            }
            else
            {
                boost::property_tree::ptree contents = getBlockJson(hash,store);
                if(contents.empty())
                {
                    res.error = true;
                    res.error_msg += "Block not found: " + hash_text + " .";
                }
                else
                {
                    blocks.push_back (std::make_pair(hash_text, contents));
                }
            }
        }
        res.contents.add_child ("blocks", blocks);
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}

logos::rpc_handler::RpcResponse<boost::property_tree::ptree>
logos::rpc_handler::blocks_exist(
        const boost::property_tree::ptree& request,
        logos::block_store& store)
{
    RpcResponse<boost::property_tree::ptree> res;
    res.error = false;
    try
    {
        std::vector<std::string> hashes;
        bool exist (true);
        logos::transaction transaction (store.environment, nullptr, false);
        for (const boost::property_tree::ptree::value_type & hashes : request.get_child ("hashes"))
        {
            std::string hash_text = hashes.second.data ();
            logos::uint256_union hash;
            if (hash.decode_hex (hash_text))
            {
                res.error = true;
                res.error_msg += "Bad hash number: " + hash_text + " .";
            }
            else
            {
                if (!store.request_exists(hash))
                {
                    exist = false;
                    break;
                }
            }
        }
        res.contents.put ("exist", exist ? "1" : "0");
    }
    catch(std::exception& e)
    {
        res.error = true;
        res.error_msg = e.what();
    }
    return res;
}



logos::payment_observer::payment_observer (std::function<void(boost::property_tree::ptree const &)> const & response_a, logos::rpc & rpc_a, logos::account const & account_a, logos::amount const & amount_a) :
rpc (rpc_a),
account (account_a),
amount (amount_a),
response (response_a)
{
    completed.clear ();
}

void logos::payment_observer::start (uint64_t timeout)
{
    auto this_l (shared_from_this ());
    rpc.node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout), [this_l]() {
        this_l->complete (logos::payment_status::nothing);
    });
}

logos::payment_observer::~payment_observer ()
{
}

void logos::payment_observer::observe ()
{
//    if (rpc.node.balance (account) >= amount.number ())
//    {
//        complete (logos::payment_status::success);
//    }
}

void logos::payment_observer::complete (logos::payment_status status)
{
    auto already (completed.test_and_set ());
    if (!already)
    {
        if (rpc.node.config.logging.log_rpc ())
        {
            BOOST_LOG (rpc.node.log) << boost::str (boost::format ("Exiting payment_observer for account %1% status %2%") % account.to_account () % static_cast<unsigned> (status));
        }
        switch (status)
        {
            case logos::payment_status::nothing:
            {
                boost::property_tree::ptree response_l;
                response_l.put ("status", "nothing");
                response (response_l);
                break;
            }
            case logos::payment_status::success:
            {
                boost::property_tree::ptree response_l;
                response_l.put ("status", "success");
                response (response_l);
                break;
            }
            default:
            {
                error_response (response, "Internal payment error");
                break;
            }
        }
        std::lock_guard<std::mutex> lock (rpc.mutex);
        assert (rpc.payment_observers.find (account) != rpc.payment_observers.end ());
        rpc.payment_observers.erase (account);
    }
}

std::unique_ptr<logos::rpc> logos::get_rpc (boost::asio::io_service & service_a, logos::node & node_a, logos::rpc_config const & config_a)
{
    std::unique_ptr<rpc> impl;

    if (config_a.secure.enable)
    {
#ifdef LOGOS_SECURE_RPC
        impl.reset (new rpc_secure (service_a, node_a, config_a));
#else
        std::cerr << "RPC configured for TLS, but the node is not compiled with TLS support" << std::endl;
#endif
    }
    else
    {
        impl.reset (new rpc (service_a, node_a, config_a));
    }

    return impl;
}
