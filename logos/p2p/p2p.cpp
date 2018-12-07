// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include "p2p.h"

#include <addrdb.h>
#include <addrman.h>
#include <chainparams.h>
#include <clientversion.h>
#include <compat/sanity.h>
#include <fs.h>
#include <netbase.h>
#include <net.h>
#include <net_processing.h>
#include <propagate.h>
#include <shutdown.h>
#include <timedata.h>
#include <ui_interface.h>
#include <util.h>
#include <utilstrencodings.h>
#include <warnings.h>
#include <stdint.h>
#include <stdio.h>

#ifndef WIN32
#include <signal.h>
#include <sys/stat.h>
#endif

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

extern CCriticalSection cs_main;

const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;

void p2p_interface::TraverseCommandLineOptions(std::function<void(const char *option, const char *description, int flags)> callback) {
    const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);
    const auto regtestBaseParams = CreateBaseChainParams(CBaseChainParams::REGTEST);
    const auto defaultChainParams = CreateChainParams(CBaseChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CBaseChainParams::TESTNET);
    const auto regtestChainParams = CreateChainParams(CBaseChainParams::REGTEST);

#define Arg(a, b, c, d, e) callback(a, ((std::string)b).c_str(), e)
#include "options.h"
#undef Arg
}

class p2p_internal {
private:

	p2p_interface *interface;
	bool fFeeEstimatesInitialized;
	const bool DEFAULT_REST_ENABLE;
	const bool DEFAULT_STOPAFTERBLOCKIMPORT;
	const char* FEE_ESTIMATES_FILENAME;
	ServiceFlags nLocalServices;
	boost::asio::io_service *io_service;
	int nMaxConnections;
	int nUserMaxConnections;
	int nFD;
	boost::thread_group threadGroup;
	std::unique_ptr<CConnman> g_connman;
	std::unique_ptr<PeerLogicValidation> peerLogic;
	PropagateStore store;

public:
	p2p_internal(p2p_interface *p2p, p2p_config &config) :
		interface(p2p),
		fFeeEstimatesInitialized(false),
		DEFAULT_REST_ENABLE(false),
		DEFAULT_STOPAFTERBLOCKIMPORT(false),
		FEE_ESTIMATES_FILENAME("fee_estimates.dat"),
		nLocalServices(ServiceFlags(NODE_NETWORK | NODE_NETWORK_LIMITED)),
		io_service((boost::asio::io_service *)config.boost_io_service)
		{}

	~p2p_internal(){}

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

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
    InterruptMapPort();
    if (g_connman)
        g_connman->Interrupt();
}

void Shutdown()
{
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which initialization failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    RenameThread(PACKAGE_NAME "-shutoff");
    StopMapPort();

    // Because these depend on each-other, we make sure that neither can be
    // using the other before destroying them.
    if (g_connman) g_connman->Stop();

    // After everything has been shut down, but before things get flushed, stop the
    // CScheduler/checkqueue threadGroup
    threadGroup.interrupt_all();
    threadGroup.join_all();

    // After the threads that potentially access these pointers have been stopped,
    // destruct and reset all to nullptr.
    peerLogic.reset();
    g_connman.reset();

    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do.
 * The execution context the handler is invoked in is not guaranteed,
 * so we restrict handler operations to just touching variables:
 */
#ifndef WIN32
static void HandleSIGTERM(int)
{
    StartShutdown();
}

static void HandleSIGHUP(int)
{
}
#else
static BOOL WINAPI consoleCtrlHandler(DWORD dwCtrlType)
{
    StartShutdown();
    Sleep(INFINITE);
    return true;
}
#endif

#ifndef WIN32
static void registerSignalHandler(int signal, void(*handler)(int))
{
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signal, &sa, nullptr);
}
#endif

void SetupServerArgs()
{
    const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);
    const auto regtestBaseParams = CreateBaseChainParams(CBaseChainParams::REGTEST);
    const auto defaultChainParams = CreateChainParams(CBaseChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CBaseChainParams::TESTNET);
    const auto regtestChainParams = CreateChainParams(CBaseChainParams::REGTEST);

    // Hidden Options
    std::vector<std::string> hidden_args = {"-benchmark", "-debugnet", "-help", "-whitelistalwaysrelay"};

#define Arg(a, b, c, d, e) gArgs.AddArg(std::string("-") + a + (e ? "=<arg>" : ""), b, c, d);
#include "options.h"
#undef Arg

    // Add the hidden options
    gArgs.AddHiddenArgs(hidden_args);
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

    if (!Random_SanityCheck()) {
        InitError("OS cryptographic RNG sanity check failure. Aborting.");
        return false;
    }

    return true;
}

// Parameter interaction based on rules
void InitParameterInteraction()
{
    // when specifying an explicit binding address, you want to listen on it
    if (gArgs.IsArgSet("-bind")) {
        if (gArgs.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (gArgs.IsArgSet("-whitebind")) {
        if (gArgs.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (gArgs.IsArgSet("-connect")) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (gArgs.SoftSetBoolArg("-dnsseed", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    if (!gArgs.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (gArgs.SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -upnp=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-listenonion", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (gArgs.IsArgSet("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    // Forcing relay from whitelisted hosts implies we will accept relays from them in the first place.
    if (gArgs.GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY)) {
        if (gArgs.SoftSetBoolArg("-whitelistrelay", true))
            LogPrintf("%s: parameter interaction: -whitelistforcerelay=1 -> setting -whitelistrelay=1\n", __func__);
    }

    // Warn if network-specific options (-addnode, -connect, etc) are
    // specified in default section of config file, but not overridden
    // on the command line or in this network's section of the config file.
    gArgs.WarnForSectionOnlyArgs();
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
    std::string version_string = FormatFullVersion();
#ifdef DEBUG
    version_string += " (debug build)";
#else
    version_string += " (release build)";
#endif
    LogPrintf(PACKAGE_NAME " version %s\n", version_string);
}

[[noreturn]] static void new_handler_terminate()
{
    // Rather than throwing std::bad-alloc if allocation fails, terminate
    // immediately to (try to) avoid chain corruption.
    // Since LogPrintf may itself allocate memory, set the handler directly
    // to terminate first.
    std::set_new_handler(std::terminate);
    LogPrintf("Error: Out of memory. Terminating.\n");

    // The log was successful, terminate now.
    std::terminate();
}

bool AppInitBasicSetup()
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0));
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
    // We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
    // which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != nullptr) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif

    if (!SetupNetworking())
        return InitError("Initializing networking failed");

#ifndef WIN32

    // Clean shutdown on SIGTERM
    registerSignalHandler(SIGTERM, HandleSIGTERM);
    registerSignalHandler(SIGINT, HandleSIGTERM);

    // Reopen debug.log on SIGHUP
    registerSignalHandler(SIGHUP, HandleSIGHUP);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#else
    SetConsoleCtrlHandler(consoleCtrlHandler, true);
#endif

    std::set_new_handler(new_handler_terminate);

    return true;
}

bool AppInitParameterInteraction()
{
    const CChainParams& chainparams = Params();
    // ********************************************************* Step 2: parameter interactions

    // also see: InitParameterInteraction()

    // -bind and -whitebind can't be set when not listening
    size_t nUserBind = gArgs.GetArgs("-bind").size() + gArgs.GetArgs("-whitebind").size();
    if (nUserBind != 0 && !gArgs.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        return InitError("Cannot set -bind or -whitebind together with -listen=0");
    }

    // Make sure enough file descriptors are available
    int nBind = std::max(nUserBind, size_t(1));
    nUserMaxConnections = gArgs.GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS);
    nMaxConnections = std::max(nUserMaxConnections, 0);

    // Trim requested connection counts, to fit into system limitations
    // <int> in std::min<int>(...) to work around FreeBSD compilation issue described in #2695
    nMaxConnections = std::max(std::min<int>(nMaxConnections, FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS), 0);
    nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS + MAX_ADDNODE_CONNECTIONS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    nMaxConnections = std::min(nFD - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS, nMaxConnections);

    if (nMaxConnections < nUserMaxConnections)
        InitWarning(strprintf(_("Reducing -maxconnections from %d to %d, because of system limitations."), nUserMaxConnections, nMaxConnections));

    // ********************************************************* Step 3: parameter-to-internal-flags
    if (gArgs.IsArgSet("-debug")) {
        // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
        const std::vector<std::string> categories = gArgs.GetArgs("-debug");

        if (std::none_of(categories.begin(), categories.end(),
            [](std::string cat){return cat == "0" || cat == "none";})) {
            for (const auto& cat : categories) {
                if (!g_logger->EnableCategory(cat)) {
                    InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debug", cat));
                }
            }
        }
    }

    // Now remove the logging categories which were explicitly excluded
    for (const std::string& cat : gArgs.GetArgs("-debugexclude")) {
        if (!g_logger->DisableCategory(cat)) {
            InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debugexclude", cat));
        }
    }

    // Check for -debugnet
    if (gArgs.GetBoolArg("-debugnet", false))
        InitWarning(_("Unsupported argument -debugnet ignored, use -debug=net."));

    if (gArgs.GetBoolArg("-benchmark", false))
        InitWarning(_("Unsupported argument -benchmark ignored, use -debug=bench."));

    if (gArgs.GetBoolArg("-whitelistalwaysrelay", false))
        InitWarning(_("Unsupported argument -whitelistalwaysrelay ignored, use -whitelistrelay and/or -whitelistforcerelay."));

    nConnectTimeout = gArgs.GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

    return true;
}

bool LockDataDirectory(bool probeOnly)
{
    // Make sure only a single Bitcoin process is using the data directory.
    fs::path datadir = GetDataDir();
    if (!DirIsWritable(datadir)) {
        return InitError(strprintf(_("Cannot write to data directory '%s'; check permissions."), datadir.string()));
    }
    if (!LockDirectory(datadir, ".lock", probeOnly)) {
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. %s is probably already running."), datadir.string(), _(PACKAGE_NAME)));
    }
    return true;
}

bool AppInitSanityChecks()
{
    // ********************************************************* Step 4: sanity checks

    // Initialize elliptic curve code
    std::string hash512_algo = Hash512AutoDetect();
    LogPrintf("Using the '%s' Blake2b implementation\n", hash512_algo);
    RandomInit();

    // Sanity check
    if (!InitSanityCheck())
        return InitError(strprintf(_("Initialization sanity check failed. %s is shutting down."), _(PACKAGE_NAME)));

    // Probe the data directory lock to give an early error message, if possible
    // We cannot hold the data directory lock here, as the forking for daemon() hasn't yet happened,
    // and a fork will cause weird behavior to it.
    return LockDataDirectory(true);
}

bool AppInitLockDataDirectory()
{
    // After daemonization get the data directory lock again and hold on to it until exit
    // This creates a slight window for a race condition to happen, however this condition is harmless: it
    // will at most make us exit without printing a message to console.
    if (!LockDataDirectory(false)) {
        // Detailed error printed inside LockDataDirectory
        return false;
    }
    return true;
}

bool AppInitMain(p2p_config &config)
{
    const CChainParams& chainparams = Params();
    // ********************************************************* Step 4a: application initialization
#ifndef WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif
    LogPrintf("Startup time: %s\n", FormatISO8601DateTime(GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", GetDataDir().string());
    LogPrintf("Using config file %s\n", GetConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME)).string());
    LogPrintf("Using at most %i automatic connections (%i file descriptors available)\n", nMaxConnections, nFD);

    // ********************************************************* Step 6: network initialization
    // Note that we absolutely cannot open any actual connections
    // until the very end ("start node") as the UTXO/block state
    // is not yet setup and may end up being set up twice if we
    // need to reindex later.

    assert(!g_connman);
    g_connman = std::unique_ptr<CConnman>(new CConnman(GetRand(std::numeric_limits<uint64_t>::max()), GetRand(std::numeric_limits<uint64_t>::max())));
    CConnman& connman = *g_connman;
    connman.p2p = interface;
    connman.p2p_store = &store;
    connman.io_service = io_service;
    connman.scheduleAfter = config.scheduleAfterMs;

    peerLogic.reset(new PeerLogicValidation(&connman, gArgs.GetBoolArg("-enablebip61", DEFAULT_ENABLE_BIP61)));

    // sanitize comments per BIP-0014, format user agent and check total size
    std::vector<std::string> uacomments;
    for (const std::string& cmt : gArgs.GetArgs("-uacomment")) {
        if (cmt != SanitizeString(cmt, SAFE_CHARS_UA_COMMENT))
            return InitError(strprintf(_("User Agent comment (%s) contains unsafe characters."), cmt));
        uacomments.push_back(cmt);
    }
    strSubVersion = FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH) {
        return InitError(strprintf(_("Total length of network version string (%i) exceeds maximum length (%i). Reduce the number or size of uacomments."),
            strSubVersion.size(), MAX_SUBVERSION_LENGTH));
    }

    if (gArgs.IsArgSet("-onlynet")) {
        std::set<enum Network> nets;
        for (const std::string& snet : gArgs.GetArgs("-onlynet")) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    // Check for host lookup allowed before parsing any network related parameters
    fNameLookup = gArgs.GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);

    // see Step 2: parameter interactions for more information about these
    fListen = gArgs.GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = gArgs.GetBoolArg("-discover", true);

    for (const std::string& strAddr : gArgs.GetArgs("-externalip")) {
        CService addrLocal;
        if (Lookup(strAddr.c_str(), addrLocal, GetListenPort(), fNameLookup) && addrLocal.IsValid())
            AddLocal(addrLocal, LOCAL_MANUAL);
        else
            return InitError(ResolveErrMsg("externalip", strAddr));
    }

    uint64_t nMaxOutboundLimit = 0; //unlimited unless -maxuploadtarget is set
    uint64_t nMaxOutboundTimeframe = MAX_UPLOAD_TIMEFRAME;

    if (gArgs.IsArgSet("-maxuploadtarget")) {
        nMaxOutboundLimit = gArgs.GetArg("-maxuploadtarget", DEFAULT_MAX_UPLOAD_TARGET)*1024*1024;
    }

    Discover();

    // Map ports with UPnP
    if (gArgs.GetBoolArg("-upnp", DEFAULT_UPNP)) {
        StartMapPort();
    }

    CConnman::Options connOptions;
    connOptions.nLocalServices = nLocalServices;
    connOptions.nMaxConnections = nMaxConnections;
    connOptions.nMaxOutbound = std::min(MAX_OUTBOUND_CONNECTIONS, connOptions.nMaxConnections);
    connOptions.nMaxAddnode = MAX_ADDNODE_CONNECTIONS;
    connOptions.nMaxFeeler = 1;
    connOptions.uiInterface = &uiInterface;
    connOptions.m_msgproc = peerLogic.get();
    connOptions.nSendBufferMaxSize = 1000*gArgs.GetArg("-maxsendbuffer", DEFAULT_MAXSENDBUFFER);
    connOptions.nReceiveFloodSize = 1000*gArgs.GetArg("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER);
    connOptions.m_added_nodes = gArgs.GetArgs("-addnode");

    connOptions.nMaxOutboundTimeframe = nMaxOutboundTimeframe;
    connOptions.nMaxOutboundLimit = nMaxOutboundLimit;

    for (const std::string& strBind : gArgs.GetArgs("-bind")) {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false)) {
            return InitError(ResolveErrMsg("bind", strBind));
        }
        connOptions.vBinds.push_back(addrBind);
    }
    for (const std::string& strBind : gArgs.GetArgs("-whitebind")) {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, 0, false)) {
            return InitError(ResolveErrMsg("whitebind", strBind));
        }
        if (addrBind.GetPort() == 0) {
            return InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
        }
        connOptions.vWhiteBinds.push_back(addrBind);
    }

    for (const auto& net : gArgs.GetArgs("-whitelist")) {
        CSubNet subnet;
        LookupSubNet(net.c_str(), subnet);
        if (!subnet.IsValid())
            return InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
        connOptions.vWhitelistedRange.push_back(subnet);
    }

    connOptions.vSeedNodes = gArgs.GetArgs("-seednode");

    // Initiate outbound connections unless connect=0
    connOptions.m_use_addrman_outgoing = !gArgs.IsArgSet("-connect");
    if (!connOptions.m_use_addrman_outgoing) {
        const auto connect = gArgs.GetArgs("-connect");
        if (connect.size() != 1 || connect[0] != "0") {
            connOptions.m_specified_outgoing = connect;
        }
    }
    if (!connman.Start(connOptions)) {
        return false;
    }

    // ********************************************************* Step 13: finished

    uiInterface.InitMessage(_("Done loading"));

    return true;
}

bool Find(PropagateMessage &mess) {
	return store.Find(mess);
}

bool Propagate(PropagateMessage &mess) {
	return store.Insert(mess);
}

}; // end of the class p2p_internal


bool p2p_interface::Init(p2p_config &config) {
	if (p2p) return false;
	uiInterface.config = &config;
	SetupEnvironment();
	p2p = new p2p_internal(this, config);
	if (!p2p) return false;
	p2p->SetupServerArgs();
	std::string error;
	if (!gArgs.ParseParameters(config.argc, config.argv, error)) {
		config.init_print((std::string("Error parsing command line arguments: ") + error).c_str());
		return false;
	}
	g_p2p_lmdb_env = config.lmdb_env;
	g_p2p_lmdb_dbi = config.lmdb_dbi;

	// Process help and version before taking care about datadir
	if (HelpRequested(gArgs)) {
		std::string strUsage = PACKAGE_NAME " daemon version " + FormatFullVersion() + "\n";

		strUsage += "\nUsage:  ";
		strUsage += config.argv[0];
		strUsage += " [options]                     Start " PACKAGE_NAME " daemon\n";
		strUsage += "\n" + gArgs.GetHelpMessage();

		config.init_print(strUsage.c_str());
		return false;
	}

	if (!fs::is_directory(GetDataDir(false)))
	{
		config.init_print((std::string("Error: Specified data directory \"") + GetDataDir().string() + "\" does not exist.").c_str());
		return false;
	}
	if (!gArgs.ReadConfigFiles(error, true)) {
		config.init_print((std::string("Error reading configuration file: ") + error).c_str());
		return false;
	}
	// Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
	try {
		SelectParams(gArgs.GetChainName());
	} catch (const std::exception& e) {
		config.init_print((std::string("Error: ") + e.what()).c_str());
		return false;
	}

	// Error out when loose non-argument tokens are encountered on command line
	for (int i = 1; i < config.argc; i++) {
		if (!IsSwitchChar(config.argv[i][0])) {
			config.init_print((std::string("Error: Command line contains unexpected token '") + config.argv[i]
				+ "', see " + config.argv[0] + " --help for a list of options.").c_str());
			return false;
		}
	}

	// -server defaults to true for bitcoind but not for the GUI so do this here
	gArgs.SoftSetBoolArg("-server", true);

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

void p2p_interface::Shutdown() {
	if (p2p) {
		p2p->Interrupt();
		p2p->Shutdown();
		p2p = 0;
	}
}

bool p2p_interface::PropagateMessage(const void *message, unsigned size) {
	if (!p2p) return false;
	struct PropagateMessage mess(message, size);
	if (p2p->Find(mess) || !ReceiveMessageCallback(message, size) || !p2p->Propagate(mess)) {
		return false;
	}
	return true;
}
