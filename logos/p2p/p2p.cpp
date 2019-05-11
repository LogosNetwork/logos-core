// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/shared_ptr.hpp>

#include <config/bitcoin-config.h>
#include <p2p.h>
#include <addrdb.h>
#include <addrman.h>
#include <chainparams.h>
#include <compat/sanity.h>
#include <netbase.h>
#include <net.h>
#include <net_processing.h>
#include <propagate.h>
#include <timedata.h>
#include <ui_interface.h>
#include <util.h>
#include <utilstrencodings.h>

const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;

void p2p_interface::TraverseCommandLineOptions(std::function<void(const char *option, const char *description, int flags)> callback)
{
    const auto defaultChainParams = CreateChainParams(CChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CChainParams::TESTNET);
    const auto regtestChainParams = CreateChainParams(CChainParams::REGTEST);

#define Arg(a, b, c, d, e) callback(a, ((std::string)b).c_str(), e)
#include "options.h"
#undef Arg
}

class p2p_internal
{
public:
    BCLog::Logger                           logger_;
private:
    p2p_interface &                         interface;
    ArgsManager                             Args;
    TimeData                                timeData;
    Random                                  random_;
    boost::asio::io_service *               io_service;
    int                                     nMaxConnections;
    int                                     nUserMaxConnections;
    int                                     nFD;
    std::unique_ptr<CConnman>               g_connman;
    std::unique_ptr<PeerLogicValidation>    peerLogic;
    PropagateStore                          store;
    int                                     nConnectTimeout;
    bool                                    fNameLookup;
    bool                                    fLogIPs;
    CCriticalSection                        cs_Shutdown;

public:
    CClientUIInterface                      uiInterface;
    std::shared_ptr<CChainParams>           chainParams;

    p2p_internal(p2p_interface & p2p,
                 p2p_config & config)
        : interface(p2p)
        , Args(logger_)
        , timeData(logger_)
        , random_(logger_)
        , io_service((boost::asio::io_service *)config.boost_io_service)
        , nConnectTimeout(DEFAULT_CONNECT_TIMEOUT)
        , fNameLookup(DEFAULT_NAME_LOOKUP)
        , fLogIPs(DEFAULT_LOGIPS)
        , uiInterface(config)
    {
    }

    ~p2p_internal()
    {
    }

#define MIN_CORE_FILEDESCRIPTORS 150

    //////////////////////////////////////////////////////////////////////////////
    //
    // Shutdown
    //

    //
    // Thread management and startup/shutdown:
    //
    // The network-processing threads are all part of a thread group
    // created by AppInit() or the Qt main() function.
    //
    // A clean exit happens when StartShutdown() or the SIGTERM
    // signal handler sets ShutdownRequested(), which makes main thread's
    // WaitForShutdown() interrupts the thread group.
    // And then, WaitForShutdown() makes all other on-going threads
    // in the thread group join the main thread.
    // Shutdown() is then called to clean up database connections, and stop other
    // threads that should only be stopped after the main network-processing
    // threads have exited.
    //
    // Shutdown for Qt is very similar, only it uses a QTimer to detect
    // ShutdownRequested() getting set, and then does the normal Qt
    // shutdown thing.
    //

    void Interrupt()
    {
        if (g_connman)
            g_connman->Interrupt();
    }

    void Shutdown()
    {
        LogPrintf("%s: In progress...\n", __func__);
        TRY_LOCK(cs_Shutdown, lockShutdown);
        if (!lockShutdown)
            return;

        /// Note: Shutdown() must be able to handle cases in which initialization failed part of the way,
        /// for example if the data directory was found to be locked.
        /// Be sure that anything that writes files or flushes caches only does this if the respective
        /// module was initialized.
        RenameThread(PACKAGE_NAME "-shutoff");

        // Because these depend on each-other, we make sure that neither can be
        // using the other before destroying them.
        if (g_connman) g_connman->Stop();

        // After the threads that potentially access these pointers have been stopped,
        // destruct and reset all to nullptr.
        peerLogic.reset();
        g_connman.reset();

        LogPrintf("%s: done\n", __func__);
    }

    ArgsManager &getArgs()
    {
        return Args;
    }

    static void registerSignalHandler(int signal, void(*handler)(int))
    {
        struct sigaction sa;
        sa.sa_handler = handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(signal, &sa, nullptr);
    }

    void SetupServerArgs()
    {
        const auto defaultChainParams = CreateChainParams(CChainParams::MAIN);
        const auto testnetChainParams = CreateChainParams(CChainParams::TESTNET);
        const auto regtestChainParams = CreateChainParams(CChainParams::REGTEST);

#define Arg(a, b, c, d, e) Args.AddArg(std::string("-") + a + (e ? "=<arg>" : ""), b, c, d);
#include "options.h"
#undef Arg
    }

    std::string LicenseInfo()
    {

        return CopyrightHolders(strprintf(_("Copyright (C) %i-%i"), FIRST_COPYRIGHT_YEAR, COPYRIGHT_YEAR) + " ") + "\n" +
                "\n" +
                strprintf(_("Please contribute if you find %s useful. "
                            "Visit <%s> for further information about the software."),
                          PACKAGE_NAME, URL_WEBSITE) +
                "\n" +
                strprintf(_("The source code is available from <%s>."),
                          URL_SOURCE_CODE) +
                "\n" +
                "\n" +
                _("This is experimental software.") + "\n" +
                strprintf(_("Distributed under the MIT software license, see the accompanying file %s or %s"), "COPYING", "<https://opensource.org/licenses/MIT>") + "\n" +
                "\n" +
                strprintf(_("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit %s and cryptographic software written by Eric Young and UPnP software written by Thomas Bernard."), "<https://www.openssl.org>") +
                "\n";
    }

    /** Sanity checks
     *  Ensure that Bitcoin is running in a usable environment with all
     *  necessary library support.
     */
    bool InitSanityCheck(void)
    {
        if (!glibc_sanity_test() || !glibcxx_sanity_test())
            return false;

        if (!random_.SanityCheck())
        {
            uiInterface.InitError("OS cryptographic RNG sanity check failure. Aborting.");
            return false;
        }

        return true;
    }

    // Parameter interaction based on rules
    void InitParameterInteraction()
    {
        // when specifying an explicit binding address, you want to listen on it
        if (Args.IsArgSet("-bind"))
        {
            if (Args.SoftSetBoolArg("-listen", true))
                LogPrintf("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
        }
        if (Args.IsArgSet("-whitebind"))
        {
            if (Args.SoftSetBoolArg("-listen", true))
                LogPrintf("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
        }

        if (Args.IsArgSet("-connect"))
        {
            // when only connecting to trusted nodes, do not seed via DNS, or listen by default
            if (Args.SoftSetBoolArg("-dnsseed", false))
                LogPrintf("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
            if (Args.SoftSetBoolArg("-listen", false))
                LogPrintf("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
        }

        if (!Args.GetBoolArg("-listen", DEFAULT_LISTEN))
        {
            // do not map ports or try to retrieve public IP when not listening (pointless)
            if (Args.SoftSetBoolArg("-discover", false))
                LogPrintf("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        }

        if (Args.IsArgSet("-externalip"))
        {
            // if an explicit public IP is specified, do not try to find others
            if (Args.SoftSetBoolArg("-discover", false))
                LogPrintf("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
        }

        // Warn if network-specific options (-addnode, -connect, etc) are
        // specified in default section of config file, but not overridden
        // on the command line or in this network's section of the config file.
        Args.WarnForSectionOnlyArgs();
    }

    std::string ResolveErrMsg(const char * const optname, const std::string& strBind)
    {
        return strprintf(_("Cannot resolve -%s address: '%s'"), optname, strBind);
    }

    /**
     * Initialize global loggers.
     *
     * Note that this is called very early in the process lifetime, so you should be
     * careful about what global state you rely on here.
     */
    void InitLogging()
    {
        fLogIPs = Args.GetBoolArg("-logips", DEFAULT_LOGIPS);

        std::string version_string = FormatFullVersion();
#ifdef DEBUG
        version_string += " (debug build)";
#else
        version_string += " (release build)";
#endif
        LogPrintf(PACKAGE_NAME " version %s\n", version_string);
    }

    bool AppInitBasicSetup()
    {
        // ********************************************************* Step 1: setup

        // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
        signal(SIGPIPE, SIG_IGN);

        return true;
    }

    bool AppInitParameterInteraction()
    {
        // ********************************************************* Step 2: parameter interactions

        // also see: InitParameterInteraction()

        // -bind and -whitebind can't be set when not listening
        size_t nUserBind = Args.GetArgs("-bind").size() + Args.GetArgs("-whitebind").size();
        if (nUserBind != 0 && !Args.GetBoolArg("-listen", DEFAULT_LISTEN))
            return uiInterface.InitError("Cannot set -bind or -whitebind together with -listen=0");

        // Make sure enough file descriptors are available
        int nBind = std::max(nUserBind, size_t(1));
        nUserMaxConnections = Args.GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS);
        nMaxConnections = std::max(nUserMaxConnections, 0);

        // Trim requested connection counts, to fit into system limitations
        // <int> in std::min<int>(...) to work around FreeBSD compilation issue described in #2695
        nMaxConnections = std::max(std::min<int>(nMaxConnections, FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS), 0);
        nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS + MAX_ADDNODE_CONNECTIONS);
        if (nFD < MIN_CORE_FILEDESCRIPTORS)
            return uiInterface.InitError(_("Not enough file descriptors available."));
        nMaxConnections = std::min(nFD - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS, nMaxConnections);

        if (nMaxConnections < nUserMaxConnections)
            uiInterface.InitWarning(strprintf(_("Reducing -maxconnections from %d to %d, because of system limitations."), nUserMaxConnections, nMaxConnections));

        // ********************************************************* Step 3: parameter-to-internal-flags
        if (Args.IsArgSet("-debug"))
        {
            // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
            const std::vector<std::string> categories = Args.GetArgs("-debug");

            if (std::none_of(categories.begin(), categories.end(),
                             [](std::string cat){return cat == "0" || cat == "none";}))
            {
                for (const auto& cat : categories)
                {
                    if (!logger_.EnableCategory(cat))
                        uiInterface.InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debug", cat));
                }
            }
        }

        // Now remove the logging categories which were explicitly excluded
        for (const std::string& cat : Args.GetArgs("-debugexclude"))
        {
            if (!logger_.DisableCategory(cat))
                uiInterface.InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debugexclude", cat));
        }

        nConnectTimeout = Args.GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
        if (nConnectTimeout <= 0)
            nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

        // Option to startup with mocktime set (used for regression testing):
        timeData.SetMockTime(Args.GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

        return true;
    }

    bool AppInitSanityChecks()
    {
        // ********************************************************* Step 4: sanity checks

        // Initialize elliptic curve code
        std::string hash256_algo = Hash256AutoDetect();
        LogPrintf("Using the '%s' Blake2b implementation\n", hash256_algo);

        // Sanity check
        if (!InitSanityCheck())
            return uiInterface.InitError(strprintf(_("Initialization sanity check failed. %s is shutting down."), _(PACKAGE_NAME)));

        return true;
    }

    bool AppInitMain(p2p_config &config)
    {
        // ********************************************************* Step 4a: application initialization
        LogPrintf("Startup time: %s\n", FormatISO8601DateTime(timeData.GetTime()));
        LogPrintf("Using at most %i automatic connections (%i file descriptors available)\n", nMaxConnections, nFD);

        // ********************************************************* Step 6: network initialization
        // Note that we absolutely cannot open any actual connections
        // until the very end ("start node") as the UTXO/block state
        // is not yet setup and may end up being set up twice if we
        // need to reindex later.

        assert(!g_connman);
        g_connman = std::unique_ptr<CConnman>(new CConnman(
                                                  random_.GetRand(std::numeric_limits<uint64_t>::max()),
                                                  random_.GetRand(std::numeric_limits<uint64_t>::max()),
                                                  config,
                                                  Args,
                                                  timeData,
                                                  random_
                                                  ));
        CConnman& connman = *g_connman;
        connman.p2p = &interface;
        connman.p2p_store = &store;
        connman.io_service = io_service;
        connman.scheduleAfter = config.scheduleAfterMs;
        connman.fLogIPs = fLogIPs;
        connman.chainParams = chainParams;

        if (config.test_mode)
            return true;

        peerLogic.reset(new PeerLogicValidation(&connman, DEFAULT_ENABLE_BIP61));

        if (Args.IsArgSet("-onlynet"))
        {
            std::set<enum Network> nets;
            for (const std::string& snet : Args.GetArgs("-onlynet"))
            {
                enum Network net = ParseNetwork(snet);
                if (net == NET_UNROUTABLE)
                    return uiInterface.InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
                nets.insert(net);
            }
            for (int n = 0; n < NET_MAX; n++)
            {
                enum Network net = (enum Network)n;
                if (!nets.count(net))
                    connman.SetLimited(net);
            }
        }

        // Check for host lookup allowed before parsing any network related parameters
        fNameLookup = Args.GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);

        // see Step 2: parameter interactions for more information about these
        connman.fListen = Args.GetBoolArg("-listen", DEFAULT_LISTEN);
        connman.fDiscover = Args.GetBoolArg("-discover", true);

        for (const std::string& strAddr : Args.GetArgs("-externalip"))
        {
            CService addrLocal;
            if (Lookup(strAddr.c_str(), addrLocal, connman.GetListenPort(), fNameLookup) && addrLocal.IsValid())
                connman.AddLocal(addrLocal, LOCAL_MANUAL);
            else
                return uiInterface.InitError(ResolveErrMsg("externalip", strAddr));
        }

        uint64_t nMaxOutboundLimit = 0; //unlimited unless -maxuploadtarget is set
        uint64_t nMaxOutboundTimeframe = MAX_UPLOAD_TIMEFRAME;

        if (Args.IsArgSet("-maxuploadtarget"))
            nMaxOutboundLimit = Args.GetArg("-maxuploadtarget", DEFAULT_MAX_UPLOAD_TARGET)*1024*1024;

        connman.Discover();

        CConnman::Options connOptions;
        connOptions.nMaxConnections = nMaxConnections;
        connOptions.nMaxOutbound = std::min(MAX_OUTBOUND_CONNECTIONS, connOptions.nMaxConnections);
        connOptions.nMaxAddnode = MAX_ADDNODE_CONNECTIONS;
        connOptions.nMaxFeeler = 1;
        connOptions.uiInterface = &uiInterface;
        connOptions.m_msgproc = peerLogic.get();
        connOptions.nSendBufferMaxSize = 1000*Args.GetArg("-maxsendbuffer", DEFAULT_MAXSENDBUFFER);
        connOptions.nReceiveFloodSize = 1000*Args.GetArg("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER);
        connOptions.m_added_nodes = Args.GetArgs("-addnode");

        connOptions.nMaxOutboundTimeframe = nMaxOutboundTimeframe;
        connOptions.nMaxOutboundLimit = nMaxOutboundLimit;

        for (const std::string& strBind : Args.GetArgs("-bind"))
        {
            CService addrBind;
            if (!Lookup(strBind.c_str(), addrBind, connman.GetListenPort(), false))
                return uiInterface.InitError(ResolveErrMsg("bind", strBind));
            connOptions.vBinds.push_back(addrBind);
        }
        for (const std::string& strBind : Args.GetArgs("-whitebind"))
        {
            CService addrBind;
            if (!Lookup(strBind.c_str(), addrBind, 0, false))
                return uiInterface.InitError(ResolveErrMsg("whitebind", strBind));
            if (addrBind.GetPort() == 0)
                return uiInterface.InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
            connOptions.vWhiteBinds.push_back(addrBind);
        }

        for (const auto& net : Args.GetArgs("-whitelist"))
        {
            CSubNet subnet;
            LookupSubNet(net.c_str(), subnet);
            if (!subnet.IsValid())
                return uiInterface.InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
            connOptions.vWhitelistedRange.push_back(subnet);
        }

        connOptions.vSeedNodes = Args.GetArgs("-seednode");

        // Initiate outbound connections unless connect=0
        connOptions.m_use_addrman_outgoing = !Args.IsArgSet("-connect");
        if (!connOptions.m_use_addrman_outgoing)
        {
            const auto connect = Args.GetArgs("-connect");
            if (connect.size() != 1 || connect[0] != "0")
                connOptions.m_specified_outgoing = connect;
        }
        if (!connman.Start(connOptions))
            return false;

        // ********************************************************* Step 13: finished

        uiInterface.InitMessage(_("Done loading"));

        return true;
    }

    bool Find(PropagateMessage &mess)
    {
        return store.Find(mess);
    }

    bool Propagate(PropagateMessage &mess)
    {
        return store.Insert(mess);
    }

    int add_peers(char **nodes, uint8_t count)
    {
        if (!g_connman)
            return 0;

        std::vector<CAddress> vAddr;
        uint8_t res = 0;

        for (; res < count; ++res)
        {
            CAddress addr;
            if (!Lookup(nodes[res], addr, MAINNET_DEFAULT_PORT, false))
                break;
            vAddr.push_back(addr);
        }

        if (res)
            g_connman->AddNewAddresses(vAddr, vAddr[0], 2 * 60 * 60);

        return res;
    }

    int get_peers(int *next, char **nodes, uint8_t count)
    {
        if (!g_connman)
            return 0;

        return g_connman->addrman.get_peers(next, nodes, count);
    }

    void add_to_blacklist(const char *addr)
    {
        if (!g_connman)
            return;

        CService host;
        if (!Lookup(addr, host, 0, false))
            return;

        g_connman->Ban(host, BanReasonManuallyAdded, 0, false);
    }

    bool is_blacklisted(const char *addr)
    {
        if (!g_connman)
            return false;

        CService host;
        if (!Lookup(addr, host, 0, false))
            return false;

        return g_connman->IsBanned(host);
    }

    bool load_databases()
    {
        if (!g_connman)
            return false;

        return g_connman->LoadData();
    }

    bool save_databases()
    {
        if (!g_connman)
            return false;

        g_connman->DumpData();

        return true;
    }

}; // end of the class p2p_internal


bool p2p_interface::Init(p2p_config &config)
{
    if (p2p)
        return false;

    SetupEnvironment();

    p2p = std::make_shared<p2p_internal>(*this, config);
    if (!p2p)
        return false;

    p2p->SetupServerArgs();
    std::string error;

    if (!p2p->getArgs().ParseParameters(config.argc, config.argv, error))
    {
        p2p->uiInterface.InitError(std::string("illegal command line arguments: ") + error);
        return false;
    }

    // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
    try
    {
        p2p->chainParams = SelectParams(p2p->getArgs().GetChainName());
    }
    catch (const std::exception& e)
    {
        p2p->uiInterface.InitError(e.what());
        return false;
    }

    // Error out when loose non-argument tokens are encountered on command line
    for (int i = 1; i < config.argc; i++)
    {
        if (!IsSwitchChar(config.argv[i][0]))
        {
            p2p->uiInterface.InitError(std::string("Command line contains unexpected token '") + config.argv[i]
                                       + "', see " + config.argv[0] + " --help for a list of options.");
            return false;
        }
    }

    // -server defaults to true for bitcoind but not for the GUI so do this here
    p2p->getArgs().SoftSetBoolArg("-server", true);

    p2p->InitLogging();
    p2p->InitParameterInteraction();

    if (!p2p->AppInitBasicSetup())
    {
        // InitError will have been called with detailed error, which ends up on console
        return false;
    }

    if (!p2p->AppInitParameterInteraction())
    {
        // InitError will have been called with detailed error, which ends up on console
        return false;
    }

    if (!p2p->AppInitSanityChecks())
    {
        // InitError will have been called with detailed error, which ends up on console
        return false;
    }

    return p2p->AppInitMain(config);
}

void p2p_interface::Shutdown()
{
    if (p2p)
    {
        p2p->Interrupt();
        p2p->Shutdown();
        p2p = 0;
    }
}

bool p2p_interface::PropagateMessage(const void *message, unsigned size, bool output)
{
    if (!p2p)
    {
        BCLog::Logger logger_;
        LogPrintf("p2p_interface::PropagateMessage, null p2p\n");
        return false;
    }

    struct PropagateMessage mess(message, size);
    bool bfind=false;
    bool brecv=false;
    bool bprop=false;
    if ((bfind=p2p->Find(mess))
            || (brecv=!(output || ReceiveMessageCallback(message, size)))
            || (bprop=!p2p->Propagate(mess)))
    {
        if (!bfind)
        {
            BCLog::Logger &logger_ = p2p->logger_;
            LogPrintf("p2p_interface::PropagateMessage, failed to propagate, %d,%d\n", brecv, bprop);
        }
        return false;
    }

    return true;
}

int p2p_interface::add_peers(char **nodes, uint8_t count)
{
    if (!p2p)
        return 0;

    return p2p->add_peers(nodes, count);
}

int p2p_interface::get_peers(int *next, char **nodes, uint8_t count)
{
    if (!p2p)
        return 0;

    return p2p->get_peers(next, nodes, count);
}

void p2p_interface::add_to_blacklist(const char *addr)
{
    if (!p2p)
        return;

    p2p->add_to_blacklist(addr);
}

bool p2p_interface::is_blacklisted(const char *addr)
{
    if (!p2p)
        return false;

    return p2p->is_blacklisted(addr);
}

bool p2p_interface::load_databases()
{
    if (!p2p)
        return false;

    return p2p->load_databases();
}

bool p2p_interface::save_databases()
{
    if (!p2p)
        return false;

    return p2p->save_databases();
}
