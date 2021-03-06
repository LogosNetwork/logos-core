#include <logos/node/node.hpp>
#include <logos/daemon.hpp>

#include <argon2.h>

#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>

#include <bls/bls.hpp>
#include <signal.h>
#include <sys/wait.h>

#define REG_(name) sprintf(buf + strlen(buf), #name "=%llx, ", (unsigned long long)uc->uc_mcontext.gregs[REG_##name])

static void signal_catch(int signum, siginfo_t *info, void *context)
{
    static void *callstack[100];
    int frames, i;
    char **strs;
    Log log;
    LOG_FATAL(log) << "Signal " << signum << " delivered";
#ifdef __x86_64__
    {
    static char buf[0x100]; *buf = 0;
    ucontext_t *uc = (ucontext_t *)context;
    REG_(RIP); REG_(EFL); REG_(ERR); REG_(CR2);
    LOG_FATAL(log) << buf; *buf = 0;
    REG_(RAX); REG_(RBX); REG_(RCX); REG_(RDX); REG_(RSI); REG_(RDI); REG_(RBP); REG_(RSP);
    LOG_FATAL(log) << buf; *buf = 0;
    REG_(R8); REG_(R9); REG_(R10); REG_(R11); REG_(R12); REG_(R13); REG_(R14); REG_(R15);
    LOG_FATAL(log) << buf;
    }
#endif
    frames = backtrace(callstack, 100);
    strs = backtrace_symbols(callstack, frames);
    for (i = 0; i < frames; ++i)
    {
        LOG_FATAL(log) << strs[i];
    }
    signal(signum, SIG_DFL);
    kill(getpid(), signum);
    exit(-2);
}

static int signals_init(void)
{
    int i;
    struct sigaction sa;
    sa.sa_sigaction = signal_catch;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    for (i = 1; i < 32; ++i)
    {
        if (i != SIGURG && i != SIGCHLD && i != SIGCONT && i != SIGPIPE && i != SIGINT && i != SIGTERM && i != SIGWINCH && i != SIGHUP)
        {
            sigaction(i, &sa, 0);
        }
    }
    return 0;
}

static int angelize(void) {
    int iter = 0;
    int stat;
    pid_t childpid;
    while ((childpid = fork())) {
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        if (childpid > 0) while (waitpid(childpid, &stat, 0) == -1) {
            if (errno != EINTR) {
                abort();
            }
        }
        if (WIFEXITED(stat) && (WEXITSTATUS(stat) == 0 || WEXITSTATUS(stat) == 0xff)) {
            exit(WEXITSTATUS(stat));
        }
        sleep(10);
        iter = 1;
    }
    return iter;
}

int main (int argc, char * const * argv)
{
    int run_after_fail = angelize();
    signals_init();
    //TODO find a better place to call, as long as before the 1st BLS operation, e.g. key generation
    bls::init();

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

    if (run_after_fail)
    {
        // remove if the daemon not fail after restart with non-empty database
        boost::filesystem::remove(data_path / "data.ldb");
    }

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
