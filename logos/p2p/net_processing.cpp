// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net_processing.h>

#include <addrman.h>
#include <arith_uint256.h>
#include <chainparams.h>
#include <hash.h>
#include <netmessagemaker.h>
#include <netbase.h>
#include <random.h>
#include <reverse_iterator.h>
#include <scheduler.h>
#include <tinyformat.h>
#include <ui_interface.h>
#include <util.h>
#include <utilstrencodings.h>

#include <memory>

#if defined(NDEBUG)
//# error "Bitcoin cannot be compiled without assertions."
#endif

/** Expiration time for orphan transactions in seconds */
static constexpr int64_t ORPHAN_TX_EXPIRE_TIME = 20 * 60;
/** Minimum time between orphan transactions expire time checks in seconds */
static constexpr int64_t ORPHAN_TX_EXPIRE_INTERVAL = 5 * 60;
/** Headers download timeout expressed in microseconds
 *  Timeout = base + per_header * (expected number of headers) */
static constexpr int64_t HEADERS_DOWNLOAD_TIMEOUT_BASE = 15 * 60 * 1000000; // 15 minutes
static constexpr int64_t HEADERS_DOWNLOAD_TIMEOUT_PER_HEADER = 1000; // 1ms/header
/** Protect at least this many outbound peers from disconnection due to slow/
 * behind headers chain.
 */
static constexpr int32_t MAX_OUTBOUND_PEERS_TO_PROTECT_FROM_DISCONNECT = 4;
/** Timeout for (unprotected) outbound peers to sync to our chainwork, in seconds */
static constexpr int64_t CHAIN_SYNC_TIMEOUT = 20 * 60; // 20 minutes
/** How frequently to check for stale tips, in seconds */
static constexpr int64_t STALE_CHECK_INTERVAL = 10 * 60; // 10 minutes
/** How frequently to check for extra outbound peers and disconnect, in seconds */
static constexpr int64_t EXTRA_PEER_CHECK_INTERVAL = 45;
/** Minimum time an outbound-peer-eviction candidate must be connected for, in order to evict, in seconds */
static constexpr int64_t MINIMUM_CONNECT_TIME = 30;
/** SHA256("main address relay")[0:8] */
static constexpr uint64_t RANDOMIZER_ID_ADDRESS_RELAY = 0x3cac0035b5866b90ULL;
/// Age after which a stale block will no longer be served if requested as
/// protection against fingerprinting. Set to one month, denominated in seconds.
static constexpr int STALE_RELAY_AGE_LIMIT = 30 * 24 * 60 * 60;
/// Age after which a block is considered historical for purposes of rate
/// limiting block relay. Set to one week, denominated in seconds.
static constexpr int HISTORICAL_BLOCK_AGE = 7 * 24 * 60 * 60;

CCriticalSection cs_main;

CCriticalSection g_cs_orphans;

void EraseOrphansFor(NodeId peer);

/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch, const std::string& message="") EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Average delay between local address broadcasts in seconds. */
static constexpr unsigned int AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL = 24 * 60 * 60;
/** Average delay between peer address broadcasts in seconds. */
static const unsigned int AVG_ADDRESS_BROADCAST_INTERVAL = 30;
/** Average delay between trickled inventory transmissions in seconds.
 *  Blocks and whitelisted receivers bypass this, outbound peers get half this delay. */
static const unsigned int INVENTORY_BROADCAST_INTERVAL = 5;
/** Maximum number of inventory items to send per transmission.
 *  Limits the impact of low-fee transaction floods. */
static constexpr unsigned int INVENTORY_BROADCAST_MAX = 7 * INVENTORY_BROADCAST_INTERVAL;
/** Average delay between feefilter broadcasts in seconds. */
static constexpr unsigned int AVG_FEEFILTER_BROADCAST_INTERVAL = 10 * 60;
/** Maximum feefilter broadcast delay after significant change. */
static constexpr unsigned int MAX_FEEFILTER_CHANGE_DELAY = 5 * 60;

// Internal stuff
namespace {
    /** Number of nodes with fSyncStarted. */
    int nSyncStarted GUARDED_BY(cs_main) = 0;

    /**
     * Sources of received blocks, saved to be able to send them reject
     * messages or ban them when processing happens afterwards.
     * Set mapBlockSource[hash].second to false if the node should not be
     * punished if the block is invalid.
     */
    std::map<uint256, std::pair<NodeId, bool>> mapBlockSource GUARDED_BY(cs_main);

    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.3 MB
     */
    std::unique_ptr<CRollingBloomFilter> recentRejects GUARDED_BY(cs_main);
    uint256 hashRecentRejectsChainTip GUARDED_BY(cs_main);

    /** Stack of nodes which we have set to announce using compact blocks */
    std::list<NodeId> lNodesAnnouncingHeaderAndIDs GUARDED_BY(cs_main);

    /** Number of preferable block download peers. */
    int nPreferredDownload GUARDED_BY(cs_main) = 0;

    /** Number of peers from which we're downloading blocks. */
    int nPeersWithValidatedDownloads GUARDED_BY(cs_main) = 0;

    /** Number of outbound peers with m_chain_sync.m_protect. */
    int g_outbound_peers_with_protect_from_disconnect GUARDED_BY(cs_main) = 0;

    /** When our tip was last updated. */
    std::atomic<int64_t> g_last_tip_update(0);

    /** Relay map */

    std::atomic<int64_t> nTimeBestReceived(0); // Used only to inform the wallet of when we last received a block

    struct IteratorComparator
    {
        template<typename I>
        bool operator()(const I& a, const I& b) const
        {
            return &(*a) < &(*b);
        }
    };
} // namespace

namespace {
struct CBlockReject {
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The peer's address
    const CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    const std::string name;
    //! The best known block we know this peer has announced.
    const CBlockIndex *pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    const CBlockIndex *pindexLastCommonBlock;
    //! The best header we have sent our peer.
    const CBlockIndex *pindexBestHeaderSent;
    //! Length of current-streak of unconnecting headers announcements
    int nUnconnectingHeaders;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! When to potentially disconnect peer for stalling headers download
    int64_t nHeadersSyncTimeout;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    //! When the first entry in vBlocksInFlight started downloading. Don't care when vBlocksInFlight is empty.
    int64_t nDownloadingSince;
    int nBlocksInFlight;
    int nBlocksInFlightValidHeaders;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;
    //! Whether this peer wants invs or headers (when possible) for block announcements.
    bool fPreferHeaders;
    //! Whether this peer wants invs or cmpctblocks (when possible) for block announcements.
    bool fPreferHeaderAndIDs;
    /**
      * Whether this peer will send us cmpctblocks if we request them.
      * This is not used to gate request logic, as we really only care about fSupportsDesiredCmpctVersion,
      * but is used as a flag to "lock in" the version of compact blocks (fWantsCmpctWitness) we send.
      */
    bool fProvidesHeaderAndIDs;
    //! Whether this peer can give us witnesses
    bool fHaveWitness;
    //! Whether this peer wants witnesses in cmpctblocks/blocktxns
    bool fWantsCmpctWitness;
    /**
     * If we've announced NODE_WITNESS to this peer: whether the peer sends witnesses in cmpctblocks/blocktxns,
     * otherwise: whether this peer sends non-witnesses in cmpctblocks/blocktxns.
     */
    bool fSupportsDesiredCmpctVersion;

    /** State used to enforce CHAIN_SYNC_TIMEOUT
      * Only in effect for outbound, non-manual connections, with
      * m_protect == false
      * Algorithm: if a peer's best known block has less work than our tip,
      * set a timeout CHAIN_SYNC_TIMEOUT seconds in the future:
      *   - If at timeout their best known block now has more work than our tip
      *     when the timeout was set, then either reset the timeout or clear it
      *     (after comparing against our current tip's work)
      *   - If at timeout their best known block still has less work than our
      *     tip did when the timeout was set, then send a getheaders message,
      *     and set a shorter timeout, HEADERS_RESPONSE_TIME seconds in future.
      *     If their best known block is still behind when that new timeout is
      *     reached, disconnect.
      */
    struct ChainSyncTimeoutState {
        //! A timeout used for checking whether our peer has sufficiently synced
        int64_t m_timeout;
        //! A header with the work we require on our peer's chain
        const CBlockIndex * m_work_header;
        //! After timeout is reached, set to true after sending getheaders
        bool m_sent_getheaders;
        //! Whether this peer is protected from disconnection due to a bad/slow chain
        bool m_protect;
    };

    ChainSyncTimeoutState m_chain_sync;

    //! Time of last new block announcement
    int64_t m_last_block_announcement;

    CNodeState(CAddress addrIn, std::string addrNameIn) : address(addrIn), name(addrNameIn) {
        fCurrentlyConnected = false;
        nMisbehavior = 0;
        fShouldBan = false;
        pindexBestKnownBlock = nullptr;
        hashLastUnknownBlock.SetNull();
        pindexLastCommonBlock = nullptr;
        pindexBestHeaderSent = nullptr;
        nUnconnectingHeaders = 0;
        fSyncStarted = false;
        nHeadersSyncTimeout = 0;
        nStallingSince = 0;
        nDownloadingSince = 0;
        nBlocksInFlight = 0;
        nBlocksInFlightValidHeaders = 0;
        fPreferredDownload = false;
        fPreferHeaders = false;
        fPreferHeaderAndIDs = false;
        fProvidesHeaderAndIDs = false;
        fHaveWitness = false;
        fWantsCmpctWitness = false;
        fSupportsDesiredCmpctVersion = false;
        m_chain_sync = { 0, nullptr, false, false };
        m_last_block_announcement = 0;
    }
};

/** Map maintaining per-node state. */
static std::map<NodeId, CNodeState> mapNodeState GUARDED_BY(cs_main);

static int IsInitialBlockDownload() {
	return 0; // todo
}

static CNodeState *State(NodeId pnode) EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
    if (it == mapNodeState.end())
        return nullptr;
    return &it->second;
}

static void UpdatePreferredDownload(CNode* node, CNodeState* state) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    nPreferredDownload -= state->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

    nPreferredDownload += state->fPreferredDownload;
}

static void PushNodeVersion(CNode *pnode, CConnman* connman, int64_t nTime)
{
    ServiceFlags nLocalNodeServices = pnode->GetLocalServices();
    uint64_t nonce = pnode->GetLocalNonce();
    int nNodeStartingHeight = pnode->GetMyStartingHeight();
    NodeId nodeid = pnode->GetId();
    CAddress addr = pnode->addr;

    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService(), addr.nServices));
    CAddress addrMe = CAddress(CService(), nLocalNodeServices);

    connman->PushMessage(pnode, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERSION, PROTOCOL_VERSION, (uint64_t)nLocalNodeServices, nTime, addrYou, addrMe,
            nonce, strSubVersion, nNodeStartingHeight, ::fRelayTxes));

    if (fLogIPs) {
        LogPrint(BCLog::NET, "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION, nNodeStartingHeight, addrMe.ToString(), addrYou.ToString(), nodeid);
    } else {
        LogPrint(BCLog::NET, "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, nNodeStartingHeight, addrMe.ToString(), nodeid);
    }
}

} // namespace

// Returns true for outbound peers, excluding manual connections, feelers, and
// one-shots
static bool IsOutboundDisconnectionCandidate(const CNode *node)
{
    return !(node->fInbound || node->m_manual_connection || node->fFeeler || node->fOneShot);
}

void PeerLogicValidation::InitializeNode(CNode *pnode) {
    CAddress addr = pnode->addr;
    std::string addrName = pnode->GetAddrName();
    NodeId nodeid = pnode->GetId();
    {
        LOCK(cs_main);
        mapNodeState.emplace_hint(mapNodeState.end(), std::piecewise_construct, std::forward_as_tuple(nodeid), std::forward_as_tuple(addr, std::move(addrName)));
    }
    if(!pnode->fInbound)
        PushNodeVersion(pnode, connman, GetTime());
}

void PeerLogicValidation::FinalizeNode(NodeId nodeid, bool& fUpdateConnectionTime) {
    fUpdateConnectionTime = false;
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    if (state->fSyncStarted)
        nSyncStarted--;

    if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
        fUpdateConnectionTime = true;
    }

    EraseOrphansFor(nodeid);
    nPreferredDownload -= state->fPreferredDownload;
    nPeersWithValidatedDownloads -= (state->nBlocksInFlightValidHeaders != 0);
    assert(nPeersWithValidatedDownloads >= 0);
    g_outbound_peers_with_protect_from_disconnect -= state->m_chain_sync.m_protect;
    assert(g_outbound_peers_with_protect_from_disconnect >= 0);

    mapNodeState.erase(nodeid);

    if (mapNodeState.empty()) {
        // Do a consistency check after the last peer is removed.
//        assert(mapBlocksInFlight.empty());
        assert(nPreferredDownload == 0);
        assert(nPeersWithValidatedDownloads == 0);
        assert(g_outbound_peers_with_protect_from_disconnect == 0);
    }
    LogPrint(BCLog::NET, "Cleared nodestate for peer=%d\n", nodeid);
}

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats) {
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    if (state == nullptr)
        return false;
    stats.nMisbehavior = state->nMisbehavior;
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

void EraseOrphansFor(NodeId peer)
{
    LOCK(g_cs_orphans);
    int nErased = 0;
    if (nErased > 0) LogPrint(BCLog::MEMPOOL, "Erased %d orphan tx from peer=%d\n", nErased, peer);
}

/**
 * Mark a misbehaving peer to be banned depending upon the value of `-banscore`.
 */
void Misbehaving(NodeId pnode, int howmuch, const std::string& message) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    if (howmuch == 0)
        return;

    CNodeState *state = State(pnode);
    if (state == nullptr)
        return;

    state->nMisbehavior += howmuch;
    int banscore = gArgs.GetArg("-banscore", DEFAULT_BANSCORE_THRESHOLD);
    std::string message_prefixed = message.empty() ? "" : (": " + message);
    if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore)
    {
        LogPrint(BCLog::NET, "%s: %s peer=%d (%d -> %d) BAN THRESHOLD EXCEEDED%s\n", __func__, state->name, pnode, state->nMisbehavior-howmuch, state->nMisbehavior, message_prefixed);
        state->fShouldBan = true;
    } else
        LogPrint(BCLog::NET, "%s: %s peer=%d (%d -> %d)%s\n", __func__, state->name, pnode, state->nMisbehavior-howmuch, state->nMisbehavior, message_prefixed);
}








//////////////////////////////////////////////////////////////////////////////
//
// blockchain -> download logic notification
//

PeerLogicValidation::PeerLogicValidation(CConnman* connmanIn, CScheduler &scheduler, bool enable_bip61)
    : connman(connmanIn), m_stale_tip_check_time(0), m_enable_bip61(enable_bip61) {

    // Initialize global variables that cannot be constructed at startup.
    recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));

//    const Consensus::Params& consensusParams = Params().GetConsensus();
    // Stale tip checking and peer eviction are on two different timers, but we
    // don't want them to get out of sync due to drift in the scheduler, so we
    // combine them in one function and schedule at the quicker (peer-eviction)
    // timer.
    static_assert(EXTRA_PEER_CHECK_INTERVAL < STALE_CHECK_INTERVAL, "peer eviction timer should be less than stale tip check timer");
    scheduler.scheduleEvery(std::bind(&PeerLogicValidation::CheckForStaleTipAndEvictPeers, this/*, consensusParams*/), EXTRA_PEER_CHECK_INTERVAL * 1000);
}

// All of the following cache a recent block, and are protected by cs_most_recent_block
//static CCriticalSection cs_most_recent_block;
//static std::shared_ptr<const CBlock> most_recent_block GUARDED_BY(cs_most_recent_block);
//static std::shared_ptr<const CBlockHeaderAndShortTxIDs> most_recent_compact_block GUARDED_BY(cs_most_recent_block);
//static uint256 most_recent_block_hash GUARDED_BY(cs_most_recent_block);
//static bool fWitnessesPresentInMostRecentCompactBlock GUARDED_BY(cs_most_recent_block);

//////////////////////////////////////////////////////////////////////////////
//
// Messages
//

static void RelayAddress(const CAddress& addr, bool fReachable, CConnman* connman)
{
    unsigned int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)

    // Relay to a limited number of other nodes
    // Use deterministic randomness to send to the same nodes for 24 hours
    // at a time so the addrKnowns of the chosen nodes prevent repeats
    uint64_t hashAddr = addr.GetHash();
    const CSipHasher hasher = connman->GetDeterministicRandomizer(RANDOMIZER_ID_ADDRESS_RELAY).Write(hashAddr << 32).Write((GetTime() + hashAddr) / (24*60*60));
    FastRandomContext insecure_rand;

    std::array<std::pair<uint64_t, CNode*>,2> best{{{0, nullptr}, {0, nullptr}}};
    assert(nRelayNodes <= best.size());

    auto sortfunc = [&best, &hasher, nRelayNodes](CNode* pnode) {
        if (pnode->nVersion >= CADDR_TIME_VERSION) {
            uint64_t hashKey = CSipHasher(hasher).Write(pnode->GetId()).Finalize();
            for (unsigned int i = 0; i < nRelayNodes; i++) {
                 if (hashKey > best[i].first) {
                     std::copy(best.begin() + i, best.begin() + nRelayNodes - 1, best.begin() + i + 1);
                     best[i] = std::make_pair(hashKey, pnode);
                     break;
                 }
            }
        }
    };

    auto pushfunc = [&addr, &best, nRelayNodes, &insecure_rand] {
        for (unsigned int i = 0; i < nRelayNodes && best[i].first != 0; i++) {
            best[i].second->PushAddress(addr, insecure_rand);
        }
    };

    connman->ForEachNodeThen(std::move(sortfunc), std::move(pushfunc));
}

static uint32_t GetFetchFlags(CNode* pfrom) EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
    uint32_t nFetchFlags = 0;
    if ((pfrom->GetLocalServices() & NODE_WITNESS) && State(pfrom->GetId())->fHaveWitness) {
        nFetchFlags |= MSG_WITNESS_FLAG;
    }
    return nFetchFlags;
}

bool static ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, int64_t nTimeReceived, const CChainParams& chainparams, CConnman* connman, const std::atomic<bool>& interruptMsgProc, bool enable_bip61)
{
    LogPrint(BCLog::NET, "received: %s (%u bytes) peer=%d\n", SanitizeString(strCommand), vRecv.size(), pfrom->GetId());
    if (gArgs.IsArgSet("-dropmessagestest") && GetRand(gArgs.GetArg("-dropmessagestest", 0)) == 0)
    {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (strCommand == NetMsgType::REJECT)
    {
        if (LogAcceptCategory(BCLog::NET)) {
            try {
                std::string strMsg; unsigned char ccode; std::string strReason;
                vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

                std::ostringstream ss;
                ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

                LogPrint(BCLog::NET, "Reject %s\n", SanitizeString(ss.str()));
            } catch (const std::ios_base::failure&) {
                // Avoid feedback loops by preventing reject messages from triggering a new reject message.
                LogPrint(BCLog::NET, "Unparseable reject message received\n");
            }
        }
        return true;
    }

    if (strCommand == NetMsgType::VERSION) {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            if (enable_bip61) {
                connman->PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT, strCommand, REJECT_DUPLICATE, std::string("Duplicate version message")));
            }
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        uint64_t nServiceInt;
        ServiceFlags nServices;
        int nVersion;
        int nSendVersion;
        std::string strSubVer;
        std::string cleanSubVer;
        int nStartingHeight = -1;
        bool fRelay = true;

        vRecv >> nVersion >> nServiceInt >> nTime >> addrMe;
        nSendVersion = std::min(nVersion, PROTOCOL_VERSION);
        nServices = ServiceFlags(nServiceInt);
        if (!pfrom->fInbound)
        {
            connman->SetServices(pfrom->addr, nServices);
        }
        if (!pfrom->fInbound && !pfrom->fFeeler && !pfrom->m_manual_connection && !HasAllDesirableServiceFlags(nServices))
        {
            LogPrint(BCLog::NET, "peer=%d does not offer the expected services (%08x offered, %08x expected); disconnecting\n", pfrom->GetId(), nServices, GetDesirableServiceFlags(nServices));
            if (enable_bip61) {
                connman->PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT, strCommand, REJECT_NONSTANDARD,
                                   strprintf("Expected to offer services %08x", GetDesirableServiceFlags(nServices))));
            }
            pfrom->fDisconnect = true;
            return false;
        }

        if (nVersion < MIN_PEER_PROTO_VERSION) {
            // disconnect from peers older than this proto version
            LogPrint(BCLog::NET, "peer=%d using obsolete version %i; disconnecting\n", pfrom->GetId(), nVersion);
            if (enable_bip61) {
                connman->PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                                   strprintf("Version must be %d or greater", MIN_PEER_PROTO_VERSION)));
            }
            pfrom->fDisconnect = true;
            return false;
        }

        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(strSubVer, MAX_SUBVERSION_LENGTH);
            cleanSubVer = SanitizeString(strSubVer);
        }
        if (!vRecv.empty()) {
            vRecv >> nStartingHeight;
        }
        if (!vRecv.empty())
            vRecv >> fRelay;
        // Disconnect if we connected to ourself
        if (pfrom->fInbound && !connman->CheckIncomingNonce(nNonce))
        {
            LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            PushNodeVersion(pfrom, connman, GetAdjustedTime());

        connman->PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERACK));

        pfrom->nServices = nServices;
        pfrom->SetAddrLocal(addrMe);
        {
            LOCK(pfrom->cs_SubVer);
            pfrom->strSubVer = strSubVer;
            pfrom->cleanSubVer = cleanSubVer;
        }
        pfrom->nStartingHeight = nStartingHeight;

        // set nodes not relaying blocks and tx and not serving (parts) of the historical blockchain as "clients"
        pfrom->fClient = (!(nServices & NODE_NETWORK) && !(nServices & NODE_NETWORK_LIMITED));

        // set nodes not capable of serving the complete blockchain history as "limited nodes"
        pfrom->m_limited_node = (!(nServices & NODE_NETWORK) && (nServices & NODE_NETWORK_LIMITED));

        {
            LOCK(pfrom->cs_filter);
            pfrom->fRelayTxes = fRelay; // set to true after we get the first filter* message
        }

        // Change version
        pfrom->SetSendVersion(nSendVersion);
        pfrom->nVersion = nVersion;

        if((nServices & NODE_WITNESS))
        {
            LOCK(cs_main);
            State(pfrom->GetId())->fHaveWitness = true;
        }

        // Potentially mark this peer as a preferred download peer.
        {
        LOCK(cs_main);
        UpdatePreferredDownload(pfrom, State(pfrom->GetId()));
        }

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (fListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr, pfrom->GetLocalServices());
                FastRandomContext insecure_rand;
                if (addr.IsRoutable())
                {
                    LogPrint(BCLog::NET, "ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr, insecure_rand);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(addrMe);
                    LogPrint(BCLog::NET, "ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr, insecure_rand);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || connman->GetAddressCount() < 1000)
            {
                connman->PushMessage(pfrom, CNetMsgMaker(nSendVersion).Make(NetMsgType::GETADDR));
                pfrom->fGetAddr = true;
            }
            connman->MarkAddressGood(pfrom->addr);
        }

        std::string remoteAddr;
        if (fLogIPs)
            remoteAddr = ", peeraddr=" + pfrom->addr.ToString();

        LogPrint(BCLog::NET, "receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
                  cleanSubVer, pfrom->nVersion,
                  pfrom->nStartingHeight, addrMe.ToString(), pfrom->GetId(),
                  remoteAddr);

        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        AddTimeData(pfrom->addr, nTimeOffset);

        // If the peer is old enough to have the old alert system, send it the final alert.
        if (pfrom->nVersion <= 70012) {
            CDataStream finalAlert(ParseHex("60010000000000000000000000ffffff7f00000000ffffff7ffeffff7f01ffffff7f00000000ffffff7f00ffffff7f002f555247454e543a20416c657274206b657920636f6d70726f6d697365642c2075706772616465207265717569726564004630440220653febd6410f470f6bae11cad19c48413becb1ac2c17f908fd0fd53bdc3abd5202206d0e9c96fe88d4a0f01ed9dedae2b6f9e00da94cad0fecaae66ecf689bf71b50"), SER_NETWORK, PROTOCOL_VERSION);
            connman->PushMessage(pfrom, CNetMsgMaker(nSendVersion).Make("alert", finalAlert));
        }

        // Feeler connections exist only to verify if address is online.
        if (pfrom->fFeeler) {
            assert(pfrom->fInbound == false);
            pfrom->fDisconnect = true;
        }
        return true;
    }

    if (pfrom->nVersion == 0) {
        // Must have a version message before anything else
        LOCK(cs_main);
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }

    // At this point, the outgoing message serialization version can't change.
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());

    if (strCommand == NetMsgType::VERACK)
    {
        pfrom->SetRecvVersion(std::min(pfrom->nVersion.load(), PROTOCOL_VERSION));

        if (!pfrom->fInbound) {
            // Mark this node as currently connected, so we update its timestamp later.
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
            LogPrintf("New outbound peer connected: version: %d, blocks=%d, peer=%d%s\n",
                      pfrom->nVersion.load(), pfrom->nStartingHeight, pfrom->GetId(),
                      (fLogIPs ? strprintf(", peeraddr=%s", pfrom->addr.ToString()) : ""));
        }

        pfrom->fSuccessfullyConnected = true;
        return true;
    }

    if (!pfrom->fSuccessfullyConnected) {
        // Must have a verack message before anything else
        LOCK(cs_main);
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }

    if (strCommand == NetMsgType::ADDR) {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && connman->GetAddressCount() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20, strprintf("message addr size() = %u", vAddr.size()));
            return false;
        }

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for (CAddress& addr : vAddr)
        {
            if (interruptMsgProc)
                return true;

            // We only bother storing full nodes, though this may include
            // things which we would not make an outbound connection to, in
            // part because we may make feeler connections to them.
            if (!MayHaveUsefulAddressDB(addr.nServices) && !HasAllDesirableServiceFlags(addr.nServices))
                continue;

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                RelayAddress(addr, fReachable, connman);
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        connman->AddNewAddresses(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
        return true;
    }

    if (strCommand == NetMsgType::PROPAGATE) {
	LOCK(cs_main);
	std::vector<uint8_t> mess;
	vRecv >> mess;
	if (connman->p2p->PropagateMessage(&mess[0], mess.size())) {
		connman->p2p->ReceiveMessageCallback(&mess[0], mess.size());
	}
	return true;
    }

    if (strCommand == NetMsgType::GETADDR) {
        // This asymmetric behavior for inbound and outbound connections was introduced
        // to prevent a fingerprinting attack: an attacker can send specific fake addresses
        // to users' AddrMan and later request them by sending getaddr messages.
        // Making nodes which are behind NAT and can only make outgoing connections ignore
        // the getaddr message mitigates the attack.
        if (!pfrom->fInbound) {
            LogPrint(BCLog::NET, "Ignoring \"getaddr\" from outbound connection. peer=%d\n", pfrom->GetId());
            return true;
        }

        // Only send one GetAddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        if (pfrom->fSentAddr) {
            LogPrint(BCLog::NET, "Ignoring repeated \"getaddr\". peer=%d\n", pfrom->GetId());
            return true;
        }
        pfrom->fSentAddr = true;

        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vAddr = connman->GetAddresses();
        FastRandomContext insecure_rand;
        for (const CAddress &addr : vAddr)
            pfrom->PushAddress(addr, insecure_rand);
        return true;
    }

    if (strCommand == NetMsgType::PING) {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::PONG, nonce));
        }
        return true;
    }

    if (strCommand == NetMsgType::PONG) {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime.load(), pingUsecTime);
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            LogPrint(BCLog::NET, "pong peer=%d: %s, %x expected, %x received, %u bytes\n",
                pfrom->GetId(),
                sProblem,
                pfrom->nPingNonceSent,
                nonce,
                nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
        }
        return true;
    }

    // Ignore unknown commands for extensibility
    LogPrint(BCLog::NET, "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->GetId());
    return true;
}

static bool SendRejectsAndCheckIfBanned(CNode* pnode, CConnman* connman, bool enable_bip61) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);
    CNodeState &state = *State(pnode->GetId());

    if (state.fShouldBan) {
        state.fShouldBan = false;
        if (pnode->fWhitelisted)
            LogPrintf("Warning: not punishing whitelisted peer %s!\n", pnode->addr.ToString());
        else if (pnode->m_manual_connection)
            LogPrintf("Warning: not punishing manually-connected peer %s!\n", pnode->addr.ToString());
        else {
            pnode->fDisconnect = true;
            if (pnode->addr.IsLocal())
                LogPrintf("Warning: not banning local peer %s!\n", pnode->addr.ToString());
            else
            {
                connman->Ban(pnode->addr, BanReasonNodeMisbehaving);
            }
        }
        return true;
    }
    return false;
}

bool PeerLogicValidation::ProcessMessages(CNode* pfrom, std::atomic<bool>& interruptMsgProc)
{
    const CChainParams& chainparams = Params();
    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fMoreWork = false;

    if (pfrom->fDisconnect)
        return false;

    // Don't bother if send buffer is too full to respond anyway
    if (pfrom->fPauseSend)
        return false;

    std::list<CNetMessage> msgs;
    {
        LOCK(pfrom->cs_vProcessMsg);
        if (pfrom->vProcessMsg.empty())
            return false;
        // Just take one message
        msgs.splice(msgs.begin(), pfrom->vProcessMsg, pfrom->vProcessMsg.begin());
        pfrom->nProcessQueueSize -= msgs.front().vRecv.size() + CMessageHeader::HEADER_SIZE;
        pfrom->fPauseRecv = pfrom->nProcessQueueSize > connman->GetReceiveFloodSize();
        fMoreWork = !pfrom->vProcessMsg.empty();
    }
    CNetMessage& msg(msgs.front());

    msg.SetVersion(pfrom->GetRecvVersion());
    // Scan for message start
    if (memcmp(msg.hdr.pchMessageStart, chainparams.MessageStart(), CMessageHeader::MESSAGE_START_SIZE) != 0) {
        LogPrint(BCLog::NET, "PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n", SanitizeString(msg.hdr.GetCommand()), pfrom->GetId());
        pfrom->fDisconnect = true;
        return false;
    }

    // Read header
    CMessageHeader& hdr = msg.hdr;
    if (!hdr.IsValid(chainparams.MessageStart()))
    {
        LogPrint(BCLog::NET, "PROCESSMESSAGE: ERRORS IN HEADER %s peer=%d\n", SanitizeString(hdr.GetCommand()), pfrom->GetId());
        return fMoreWork;
    }
    std::string strCommand = hdr.GetCommand();

    // Message size
    unsigned int nMessageSize = hdr.nMessageSize;

    // Checksum
    CDataStream& vRecv = msg.vRecv;
    const uint256& hash = msg.GetMessageHash();
    if (memcmp(hash.begin(), hdr.pchChecksum, CMessageHeader::CHECKSUM_SIZE) != 0)
    {
        LogPrint(BCLog::NET, "%s(%s, %u bytes): CHECKSUM ERROR expected %s was %s\n", __func__,
           SanitizeString(strCommand), nMessageSize,
           HexStr(hash.begin(), hash.begin()+CMessageHeader::CHECKSUM_SIZE),
           HexStr(hdr.pchChecksum, hdr.pchChecksum+CMessageHeader::CHECKSUM_SIZE));
        return fMoreWork;
    }

    // Process message
    bool fRet = false;
    try
    {
        fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime, chainparams, connman, interruptMsgProc, m_enable_bip61);
        if (interruptMsgProc)
            return false;
    }
    catch (const std::ios_base::failure& e)
    {
        if (m_enable_bip61) {
            connman->PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT, strCommand, REJECT_MALFORMED, std::string("error parsing message")));
        }
        if (strstr(e.what(), "end of data"))
        {
            // Allow exceptions from under-length message on vRecv
            LogPrint(BCLog::NET, "%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
        }
        else if (strstr(e.what(), "size too large"))
        {
            // Allow exceptions from over-long size
            LogPrint(BCLog::NET, "%s(%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
        }
        else if (strstr(e.what(), "non-canonical ReadCompactSize()"))
        {
            // Allow exceptions from non-canonical encoding
            LogPrint(BCLog::NET, "%s(%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
        }
        else
        {
            PrintExceptionContinue(&e, "ProcessMessages()");
        }
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "ProcessMessages()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "ProcessMessages()");
    }

    if (!fRet) {
        LogPrint(BCLog::NET, "%s(%s, %u bytes) FAILED peer=%d\n", __func__, SanitizeString(strCommand), nMessageSize, pfrom->GetId());
    }

    LOCK(cs_main);
    SendRejectsAndCheckIfBanned(pfrom, connman, m_enable_bip61);

    return fMoreWork;
}

void PeerLogicValidation::ConsiderEviction(CNode *pto, int64_t time_in_seconds)
{
    AssertLockHeld(cs_main);

    CNodeState &state = *State(pto->GetId());
//    const CNetMsgMaker msgMaker(pto->GetSendVersion());

    if (!state.m_chain_sync.m_protect && IsOutboundDisconnectionCandidate(pto) && state.fSyncStarted) {
        // This is an outbound peer subject to disconnection if they don't
        // announce a block with as much work as the current tip within
        // CHAIN_SYNC_TIMEOUT + HEADERS_RESPONSE_TIME seconds (note: if
        // their chain has more work than ours, we should sync to it,
        // unless it's invalid, in which case we should find that out and
        // disconnect from them elsewhere).
	pto->fDisconnect = true;
    }
}

void PeerLogicValidation::EvictExtraOutboundPeers(int64_t time_in_seconds)
{
    // Check whether we have too many outbound peers
    int extra_peers = connman->GetExtraOutboundCount();
    if (extra_peers > 0) {
        // If we have more outbound peers than we target, disconnect one.
        // Pick the outbound peer that least recently announced
        // us a new block, with ties broken by choosing the more recent
        // connection (higher node id)
        NodeId worst_peer = -1;
        int64_t oldest_block_announcement = std::numeric_limits<int64_t>::max();

        LOCK(cs_main);

        connman->ForEachNode([&](CNode* pnode) {
            AssertLockHeld(cs_main);

            // Ignore non-outbound peers, or nodes marked for disconnect already
            if (!IsOutboundDisconnectionCandidate(pnode) || pnode->fDisconnect) return;
            CNodeState *state = State(pnode->GetId());
            if (state == nullptr) return; // shouldn't be possible, but just in case
            // Don't evict our protected peers
            if (state->m_chain_sync.m_protect) return;
            if (state->m_last_block_announcement < oldest_block_announcement || (state->m_last_block_announcement == oldest_block_announcement && pnode->GetId() > worst_peer)) {
                worst_peer = pnode->GetId();
                oldest_block_announcement = state->m_last_block_announcement;
            }
        });
        if (worst_peer != -1) {
            bool disconnected = connman->ForNode(worst_peer, [&](CNode *pnode) {
                AssertLockHeld(cs_main);

                // Only disconnect a peer that has been connected to us for
                // some reasonable fraction of our check-frequency, to give
                // it time for new information to have arrived.
                // Also don't disconnect any peer we're trying to download a
                // block from.
                CNodeState &state = *State(pnode->GetId());
                if (time_in_seconds - pnode->nTimeConnected > MINIMUM_CONNECT_TIME && state.nBlocksInFlight == 0) {
                    LogPrint(BCLog::NET, "disconnecting extra outbound peer=%d (last block announcement received at time %d)\n", pnode->GetId(), oldest_block_announcement);
                    pnode->fDisconnect = true;
                    return true;
                } else {
                    LogPrint(BCLog::NET, "keeping outbound peer=%d chosen for eviction (connect time: %d, blocks_in_flight: %d)\n", pnode->GetId(), pnode->nTimeConnected, state.nBlocksInFlight);
                    return false;
                }
            });
            if (disconnected) {
                // If we disconnected an extra peer, that means we successfully
                // connected to at least one peer after the last time we
                // detected a stale tip. Don't try any more extra peers until
                // we next detect a stale tip, to limit the load we put on the
                // network from these extra connections.
                connman->SetTryNewOutboundPeer(false);
            }
        }
    }
}

void PeerLogicValidation::CheckForStaleTipAndEvictPeers(/*const Consensus::Params &consensusParams*/)
{
    if (connman == nullptr) return;

    int64_t time_in_seconds = GetTime();

    EvictExtraOutboundPeers(time_in_seconds);

    if (time_in_seconds > m_stale_tip_check_time) {
        LOCK(cs_main);
        // Check whether our tip is stale, and if so, allow using an extra
        // outbound peer
	if (1 /*TipMayBeStale(consensusParams)*/) {
            LogPrintf("Potential stale tip detected, will try using extra outbound peer (last tip update: %d seconds ago)\n", time_in_seconds - g_last_tip_update);
            connman->SetTryNewOutboundPeer(true);
        } else if (connman->GetTryNewOutboundPeer()) {
            connman->SetTryNewOutboundPeer(false);
        }
        m_stale_tip_check_time = time_in_seconds + STALE_CHECK_INTERVAL;
    }
}

bool PeerLogicValidation::SendMessages(CNode* pto)
{
//    const Consensus::Params& consensusParams = Params().GetConsensus();
    {
        // Don't send anything until the version handshake is complete
        if (!pto->fSuccessfullyConnected || pto->fDisconnect)
            return true;

        // If we get here, the outgoing message serialization version is set and can't change.
        const CNetMsgMaker msgMaker(pto->GetSendVersion());

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                connman->PushMessage(pto, msgMaker.Make(NetMsgType::PING, nonce));
            } else {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                connman->PushMessage(pto, msgMaker.Make(NetMsgType::PING));
            }
        }

        TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
            return true;

        if (SendRejectsAndCheckIfBanned(pto, connman, m_enable_bip61))
            return true;
        CNodeState &state = *State(pto->GetId());

        // Address refresh broadcast
        int64_t nNow = GetTimeMicros();
        if (!IsInitialBlockDownload() && pto->nNextLocalAddrSend < nNow) {
            AdvertiseLocal(pto);
            pto->nNextLocalAddrSend = PoissonNextSend(nNow, AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL);
        }

        //
        // Message: addr
        //
        if (pto->nNextAddrSend < nNow) {
            pto->nNextAddrSend = PoissonNextSend(nNow, AVG_ADDRESS_BROADCAST_INTERVAL);
            std::vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            for (const CAddress& addr : pto->vAddrToSend)
            {
                if (!pto->addrKnown.contains(addr.GetKey()))
                {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        connman->PushMessage(pto, msgMaker.Make(NetMsgType::ADDR, vAddr));
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                connman->PushMessage(pto, msgMaker.Make(NetMsgType::ADDR, vAddr));
            // we only send the big addr message once
            if (pto->vAddrToSend.capacity() > 40)
                pto->vAddrToSend.shrink_to_fit();
        }

	//
	// Message: propagate
	//
	const PropagateMessage *promess = connman->p2p_store->GetNext(pto->next_propagate_index);
	if (promess) {
		connman->PushMessage(pto, msgMaker.Make(NetMsgType::PROPAGATE, promess->message));
	}

        // Detect whether we're stalling
        nNow = GetTimeMicros();
        if (state.nStallingSince && state.nStallingSince < nNow - 1000000 * BLOCK_STALLING_TIMEOUT) {
            // Stalling only triggers when the block download window cannot move. During normal steady state,
            // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
            // should only happen during initial block download.
            LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->GetId());
            pto->fDisconnect = true;
            return true;
        }
        // Check for headers sync timeouts
        if (state.fSyncStarted && state.nHeadersSyncTimeout < std::numeric_limits<int64_t>::max()) {
            // Detect whether this is a stalling initial-headers-sync peer
	    if (0/*pindexBestHeader->GetBlockTime() <= GetAdjustedTime() - 24*60*60*/) {
                if (nNow > state.nHeadersSyncTimeout && nSyncStarted == 1 && (nPreferredDownload - state.fPreferredDownload >= 1)) {
                    // Disconnect a (non-whitelisted) peer if it is our only sync peer,
                    // and we have others we could be using instead.
                    // Note: If all our peers are inbound, then we won't
                    // disconnect our sync peer for stalling; we have bigger
                    // problems if we can't get any outbound peers.
                    if (!pto->fWhitelisted) {
                        LogPrintf("Timeout downloading headers from peer=%d, disconnecting\n", pto->GetId());
                        pto->fDisconnect = true;
                        return true;
                    } else {
                        LogPrintf("Timeout downloading headers from whitelisted peer=%d, not disconnecting\n", pto->GetId());
                        // Reset the headers sync state so that we have a
                        // chance to try downloading from a different peer.
                        // Note: this will also result in at least one more
                        // getheaders message to be sent to
                        // this peer (eventually).
                        state.fSyncStarted = false;
                        nSyncStarted--;
                        state.nHeadersSyncTimeout = 0;
                    }
                }
            } else {
                // After we've caught up once, reset the timeout so we can't trigger
                // disconnect later.
                state.nHeadersSyncTimeout = std::numeric_limits<int64_t>::max();
            }
        }

        // Check that outbound peers have reasonable chains
        // GetTime() is used by this anti-DoS logic so we can test this using mocktime
        ConsiderEviction(pto, GetTime());
    }
    return true;
}

