#include <logos/node/node.hpp>
#include <logos/node/testing.hpp>
#include <logos/daemon.hpp>

#include <argon2.h>

#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>

#include <bls/bls.hpp>
#include <logos/lib/trace.hpp>

#include <exception>
#include <cstdlib>

int main (int argc, char * const * argv)
{
    //TODO find a better place to call, as long as before the 1st BLS operation, e.g. key generation
    bls::init();

    std::set_terminate(trace);

    boost::program_options::options_description description ("Command line options");
    logos::add_node_options (description);

    // clang-format off
    description.add_options ()
        ("help", "Print out options")
        ("version", "Prints out version")
        ("daemon", "Start node daemon")
        ("tx_acceptor", "Start standalone TxAcceptor")
        ("debug_block_count", "Display the number of block")
        ("debug_bootstrap_generate", "Generate bootstrap sequence of blocks")
        ("debug_dump_representatives", "List representatives and weights")
        ("debug_account_count", "Display the number of accounts")
        ("debug_mass_activity", "Generates fake debug activity")
        ("debug_profile_generate", "Profile work generation")
        ("debug_opencl", "OpenCL work generation")
        ("debug_profile_verify", "Profile work verification")
        ("debug_profile_kdf", "Profile kdf function")
        ("debug_verify_profile", "Profile signature verification")
        ("debug_profile_sign", "Profile signature generation")
        ("platform", boost::program_options::value<std::string> (), "Defines the <platform> for OpenCL commands")
        ("device", boost::program_options::value<std::string> (), "Defines <device> for OpenCL command")
        ("threads", boost::program_options::value<std::string> (), "Defines <threads> count for OpenCL command");
    // clang-format on

    p2p_interface::TraverseCommandLineOptions([&description](const char *option, const char *help, int flags) {
	if (flags & P2P_OPTION_MULTI)
		description.add_options()(option, boost::program_options::value<std::vector<std::string> >(), help);
	else if (flags & P2P_OPTION_ARGUMENT)
		description.add_options()(option, boost::program_options::value<std::string> (), help);
	else
		description.add_options()(option, help);
    });

    boost::program_options::variables_map vm;
    boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
    boost::program_options::notify (vm);
    int result (0);
    boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : logos::working_path ();

    if (!logos::handle_node_options (vm))
    {
    }
    else if (vm.count ("daemon") > 0)
    {
        logos_daemon::daemon daemon;
        p2p_config p2p_conf;

        int nopts = 1;
        vector<char *> opts;
        opts.push_back(strdup(argv[0]));
        p2p_interface::TraverseCommandLineOptions([&vm, &opts, &nopts](const char *option, const char *help, int flags)
        {
            if (vm.count(option) > 0)
            {
                std::string opt = std::string("-") + option;
                if (flags & P2P_OPTION_MULTI)
                {
                    auto v = vm[option].as<std::vector<std::string> >();
                    for (auto it : v)
                    {
                        opts.push_back(strdup((opt + "=" + it).c_str()));
                        nopts++;
                    }
                }
                else if (flags & P2P_OPTION_ARGUMENT)
                {
                    opts.push_back(strdup((opt + "=" + vm[option].as<std::string>()).c_str()));
                    nopts++;
                }
                else
                {
                    opts.push_back(strdup(opt.c_str()));
                    nopts++;
                }
            }
        });
        p2p_conf.argc = nopts;
        p2p_conf.argv = &opts[0];
        p2p_conf.test_mode = false;

        daemon.run (data_path, p2p_conf);
    }
    else if (vm.count ("tx_acceptor") > 0)
    {
        logos_daemon::daemon daemon;
        daemon.run_tx_acceptor (data_path);
    }
    else if (vm.count ("debug_block_count"))
    {
        logos::inactive_node node (data_path);
        logos::transaction transaction (node.node->store.environment, nullptr, false);
        std::cout << boost::str (boost::format ("Block count: %1%\n") % node.node->store.block_count (transaction).sum ());
    }
    else if (vm.count ("debug_bootstrap_generate"))
    {
        //CH fix later.
        /*if (vm.count ("key") == 1)
        {
            logos::uint256_union key;
            if (!key.decode_hex (vm["key"].as<std::string> ()))
            {
                logos::keypair genesis (key.to_string ());
                logos::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
                std::cout << "Genesis: " << genesis.prv.data.to_string () << std::endl
                          << "Public: " << genesis.pub.to_string () << std::endl
                          << "Account: " << genesis.pub.to_account () << std::endl;
                logos::keypair landing;
                std::cout << "Landing: " << landing.prv.data.to_string () << std::endl
                          << "Public: " << landing.pub.to_string () << std::endl
                          << "Account: " << landing.pub.to_account () << std::endl;
                for (auto i (0); i != 32; ++i)
                {
                    logos::keypair rep;
                    std::cout << "Rep" << i << ": " << rep.prv.data.to_string () << std::endl
                              << "Public: " << rep.pub.to_string () << std::endl
                              << "Account: " << rep.pub.to_account () << std::endl;
                }
                logos::uint128_t balance (std::numeric_limits<logos::uint128_t>::max ());
                logos::open_block genesis_block (genesis.pub, genesis.pub, genesis.pub, genesis.prv, genesis.pub, work.generate (genesis.pub));
                std::cout << genesis_block.to_json ();
                logos::block_hash previous (genesis_block.hash ());
                for (auto i (0); i != 8; ++i)
                {
                    logos::uint128_t yearly_distribution (logos::uint128_t (1) << (127 - (i == 7 ? 6 : i)));
                    auto weekly_distribution (yearly_distribution / 52);
                    for (auto j (0); j != 52; ++j)
                    {
                        assert (balance > weekly_distribution);
                        balance = balance < (weekly_distribution * 2) ? 0 : balance - weekly_distribution;
                        logos::send_block send (previous, landing.pub, balance, genesis.prv, genesis.pub, work.generate (previous));
                        previous = send.hash ();
                        std::cout << send.to_json ();
                        std::cout.flush ();
                    }
                }
            }
            else
            {
                std::cerr << "Invalid key\n";
                result = -1;
            }
        }
        else
        {
            std::cerr << "Bootstrapping requires one <key> option\n";
            result = -1;
        }*/
    }
    else if (vm.count ("debug_dump_representatives"))
    {
        logos::inactive_node node (data_path);
        logos::transaction transaction (node.node->store.environment, nullptr, false);
        logos::uint128_t total;
        for (auto i (node.node->store.representation_begin (transaction)), n (node.node->store.representation_end ()); i != n; ++i)
        {
            logos::account account (i->first.uint256 ());
            auto amount (node.node->store.representation_get (transaction, account));
            total += amount;
            std::cout << boost::str (boost::format ("%1% %2% %3%\n") % account.to_account () % amount.convert_to<std::string> () % total.convert_to<std::string> ());
        }
        std::map<logos::account, logos::uint128_t> calculated;
        for (auto i (node.node->store.latest_begin (transaction)), n (node.node->store.latest_end ()); i != n; ++i)
        {
            bool error = false;
            logos::account_info info (error, i->second);
            if(error)
            {
                std::cerr << "account_info deserialize error" << std::endl;
                std::cout << " EXIT_FAILURE<3> " << std::endl;
                //exit(-1); // RGD
            }
            logos::block_hash rep_block (node.node->ledger.representative_calculated (transaction, info.head));
            std::unique_ptr<logos::block> block (node.node->store.block_get (transaction, rep_block));
            calculated[block->representative ()] += info.balance.number ();
        }
        total = 0;
        for (auto i (calculated.begin ()), n (calculated.end ()); i != n; ++i)
        {
            total += i->second;
            std::cout << boost::str (boost::format ("%1% %2% %3%\n") % i->first.to_account () % i->second.convert_to<std::string> () % total.convert_to<std::string> ());
        }
    }
    else if (vm.count ("debug_account_count"))
    {
        logos::inactive_node node (data_path);
        logos::transaction transaction (node.node->store.environment, nullptr, false);
        std::cout << boost::str (boost::format ("Frontier count: %1%\n") % node.node->store.account_count (transaction));
    }
    else if (vm.count ("debug_mass_activity"))
    {
        logos::system system (24000, 1);
        size_t count (1000000);
        system.generate_mass_activity (count, *system.nodes[0]);
    }
    else if (vm.count ("debug_profile_kdf"))
    {
        logos::uint256_union result;
        logos::uint256_union salt (0);
        std::string password ("");
        for (; true;)
        {
            auto begin1 (std::chrono::high_resolution_clock::now ());
            auto success (argon2_hash (1, logos::wallet_store::kdf_work, 1, password.data (), password.size (), salt.bytes.data (), salt.bytes.size (), result.bytes.data (), result.bytes.size (), NULL, 0, Argon2_d, 0x10));
            auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("Derivation time: %1%us\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
        }
    }
    else if (vm.count ("debug_profile_generate"))
    {
        /*logos::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
        logos::change_block block (0, 0, logos::keypair ().prv, 0, 0);
        std::cerr << "Starting generation profiling\n";
        for (uint64_t i (0); true; ++i)
        {
            block.hashables.previous.qwords[0] += 1;
            auto begin1 (std::chrono::high_resolution_clock::now ());
            block.block_work_set (work.generate (block.root ()));
            auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
        }*/
    }
    else if (vm.count ("debug_opencl"))
    {
    /*    bool error (false);
        logos::opencl_environment environment (error);
        if (!error)
        {
            unsigned short platform (0);
            if (vm.count ("platform") == 1)
            {
                try
                {
                    platform = boost::lexical_cast<unsigned short> (vm["platform"].as<std::string> ());
                }
                catch (boost::bad_lexical_cast & e)
                {
                    std::cerr << "Invalid platform id\n";
                    result = -1;
                }
            }
            unsigned short device (0);
            if (vm.count ("device") == 1)
            {
                try
                {
                    device = boost::lexical_cast<unsigned short> (vm["device"].as<std::string> ());
                }
                catch (boost::bad_lexical_cast & e)
                {
                    std::cerr << "Invalid device id\n";
                    result = -1;
                }
            }
            unsigned threads (1024 * 1024);
            if (vm.count ("threads") == 1)
            {
                try
                {
                    threads = boost::lexical_cast<unsigned> (vm["threads"].as<std::string> ());
                }
                catch (boost::bad_lexical_cast & e)
                {
                    std::cerr << "Invalid threads count\n";
                    result = -1;
                }
            }
            if (!result)
            {
                error |= platform >= environment.platforms.size ();
                if (!error)
                {
                    error |= device >= environment.platforms[platform].devices.size ();
                    if (!error)
                    {
                        logos::logging logging;
                        auto opencl (logos::opencl_work::create (true, { platform, device, threads }, logging));
                        logos::work_pool work_pool (std::numeric_limits<unsigned>::max (), opencl ? [&opencl](logos::uint256_union const & root_a) {
                            return opencl->generate_work (root_a);
                        }
                                                                                                : std::function<boost::optional<uint64_t> (logos::uint256_union const &)> (nullptr));
                        logos::change_block block (0, 0, logos::keypair ().prv, 0, 0);
                        std::cerr << boost::str (boost::format ("Starting OpenCL generation profiling. Platform: %1%. Device: %2%. Threads: %3%\n") % platform % device % threads);
                        for (uint64_t i (0); true; ++i)
                        {
                            block.hashables.previous.qwords[0] += 1;
                            auto begin1 (std::chrono::high_resolution_clock::now ());
                            block.block_work_set (work_pool.generate (block.root ()));
                            auto end1 (std::chrono::high_resolution_clock::now ());
                            std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
                        }
                    }
                    else
                    {
                        std::cout << "Not available device id\n"
                                  << std::endl;
                        result = -1;
                    }
                }
                else
                {
                    std::cout << "Not available platform id\n"
                              << std::endl;
                    result = -1;
                }
            }
        }
        else
        {
            std::cout << "Error initializing OpenCL" << std::endl;
            result = -1;
        }*/
    }
    else if (vm.count ("debug_profile_verify"))
    {
        /*logos::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
        logos::change_block block (0, 0, logos::keypair ().prv, 0, 0);
        std::cerr << "Starting verification profiling\n";
        for (uint64_t i (0); true; ++i)
        {
            block.hashables.previous.qwords[0] += 1;
            auto begin1 (std::chrono::high_resolution_clock::now ());
            for (uint64_t t (0); t < 1000000; ++t)
            {
                block.hashables.previous.qwords[0] += 1;
                block.block_work_set (t);
                logos::work_validate (block);
            }
            auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
        }*/
    }
    else if (vm.count ("debug_verify_profile"))
    {
        logos::keypair key;
        logos::uint256_union message;
        logos::uint512_union signature;
        signature = logos::sign_message (key.prv, key.pub, message);
        auto begin (std::chrono::high_resolution_clock::now ());
        for (auto i (0u); i < 1000; ++i)
        {
            logos::validate_message (key.pub, message, signature);
        }
        auto end (std::chrono::high_resolution_clock::now ());
        std::cerr << "Signature verifications " << std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count () << std::endl;
    }
    else if (vm.count ("debug_profile_sign"))
    {
        /*std::cerr << "Starting blocks signing profiling\n";
        for (uint64_t i (0); true; ++i)
        {
            logos::keypair key;
            logos::block_hash latest (0);
            auto begin1 (std::chrono::high_resolution_clock::now ());
            for (uint64_t balance (0); balance < 1000; ++balance)
            {
                logos::send_block send (latest, key.pub, balance, key.prv, key.pub, 0);
                latest = send.hash ();
            }
            auto end1 (std::chrono::high_resolution_clock::now ());
            std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
        }*/
    }
    else if (vm.count ("version"))
    {
        std::cout << "Version " << LOGOS_VERSION_MAJOR << "." << LOGOS_VERSION_MINOR << std::endl;
    }
    else
    {
        std::cout << description << std::endl;
        result = -1;
    }
    return result;
}
