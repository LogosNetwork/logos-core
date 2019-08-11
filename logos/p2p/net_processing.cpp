// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <memory>
#include <net_processing.h>
#include <addrman.h>
#include <chainparams.h>
#include <hash.h>
#include <netmessagemaker.h>
#include <netbase.h>
#include <random.h>
#include <tinyformat.h>
#include <ui_interface.h>
#include <util.h>
#include <utilstrencodings.h>

static constexpr int64_t STALE_CHECK_INTERVAL = 10 * 60; // 10 minutes
/** How frequently to check for extra outbound peers and disconnect, in seconds */
static constexpr int64_t EXTRA_PEER_CHECK_INTERVAL = 45;
/** Minimum time an outbound-peer-eviction candidate must be connected for, in order to evict, in seconds */
static constexpr int64_t MINIMUM_CONNECT_TIME = 30;
/** SHA256("main address relay")[0:8] */
static constexpr uint64_t RANDOMIZER_ID_ADDRESS_RELAY = 0x3cac0035b5866b90ULL;

/** Average delay between local address broadcasts in seconds. */
static constexpr unsigned int AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL = 24 * 60 * 60;
/** Average delay between peer address broadcasts in seconds. */
static constexpr unsigned int AVG_ADDRESS_BROADCAST_INTERVAL = 30;

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState
{
    //! Whether we have a fully established connection.
    bool                            fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int                             nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool                            fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    const std::string               name;

    //! Time of last new block announcement
    int64_t                         m_last_block_announcement;

    std::shared_ptr<BCLog::Logger>  logger_;

    CNodeState(std::string addrNameIn)
        : name(addrNameIn)
        , fCurrentlyConnected(false)
        , nMisbehavior(0)
        , fShouldBan(false)
        , m_last_block_announcement(0)
    {
    }
};

class PeerLogicValidation_internal
{
public:
    PeerLogicValidation_internal(BCLog::Logger &logger)
        : g_last_tip_update(0)
        , logger_(logger)
    {
    }

    CCriticalSection                cs_main;

    /** When our tip was last updated. */
    std::atomic<int64_t>            g_last_tip_update;

    /** Map maintaining per-node state. */
    std::map<NodeId, CNodeState>    mapNodeState GUARDED_BY(cs_main);

    BCLog::Logger &                 logger_;

    CNodeState *State(NodeId pnode) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
        if (it == mapNodeState.end())
            return nullptr;
        return &it->second;
    }

    /**
     * Mark a misbehaving peer to be banned depending upon the value of `-banscore`.
     */
    void Misbehaving(NodeId pnode, int howmuch, int banscore, const std::string& message="") EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        if (howmuch == 0)
            return;

        CNodeState *state = State(pnode);
        if (state == nullptr)
            return;

        state->nMisbehavior += howmuch;
        std::string message_prefixed = message.empty() ? "" : (": " + message);
        if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore)
        {
            LogPrint(BCLog::NET, "%s: %s peer=%d (%d -> %d) BAN THRESHOLD EXCEEDED%s\n",
                    __func__, state->name, pnode, state->nMisbehavior-howmuch, state->nMisbehavior, message_prefixed);
            state->fShouldBan = true;
        } else
            LogPrint(BCLog::NET, "%s: %s peer=%d (%d -> %d)%s\n",
                    __func__, state->name, pnode, state->nMisbehavior-howmuch, state->nMisbehavior, message_prefixed);
    }

    void RelayAddress(const CAddress& addr, bool fReachable, CConnman* connman);

    bool ProcessMessage(std::shared_ptr<CNode> pfrom,
                        const std::string& strCommand,
                        CDataStream& vRecv,
                        int64_t nTimeReceived,
                        const CChainParams& chainparams,
                        CConnman* connman,
                        const std::atomic<bool>& interruptMsgProc,
                        bool enable_bip61);

    bool SendRejectsAndCheckIfBanned(std::shared_ptr<CNode> pnode,
                                     CConnman* connman,
                                     bool enable_bip61) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    void PushNodeVersion(std::shared_ptr<CNode> pnode, CConnman* connman, int64_t nTime);
};

void PeerLogicValidation_internal::PushNodeVersion(std::shared_ptr<CNode> pnode, CConnman* connman, int64_t nTime)
{
    uint64_t nonce = pnode->GetLocalNonce();
    int nNodeStartingHeight = -1; /* we do not change format of the message and remain this field constantly equal to -1 */
    NodeId nodeid = pnode->GetId();
    CAddress addr = pnode->addr;

    CAddress addrYou = (addr.IsRoutable() ? addr : CAddress());
    CAddress addrMe;

    connman->PushMessage(pnode, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERSION,
                                                                      PROTOCOL_VERSION,
                                                                      (uint64_t)0,
                                                                      nTime,
                                                                      addrYou,
                                                                      addrMe,
                                                                      nonce,
                                                                      connman->strSubVersion,
                                                                      nNodeStartingHeight,
                                                                      true));

    if (connman->fLogIPs)
        LogPrint(BCLog::NET, "send version message: version %d, us=%s, them=%s, peer=%d\n",
                 PROTOCOL_VERSION, addrMe.ToString(), addrYou.ToString(), nodeid);
    else
        LogPrint(BCLog::NET, "send version message: version %d, us=%s, peer=%d\n",
                 PROTOCOL_VERSION, addrMe.ToString(), nodeid);
}

bool PeerLogicValidation::TipMayBeStale(int nPowTargetSpacing) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(internal->cs_main);
    if (internal->g_last_tip_update == 0)
        internal->g_last_tip_update = connman->timeData.GetTime();
    return internal->g_last_tip_update < connman->timeData.GetTime() - nPowTargetSpacing * 3 /* && mapBlocksInFlight.empty() */;
}

// Returns true for outbound peers, excluding manual connections, feelers, and
// one-shots
static bool IsOutboundDisconnectionCandidate(std::shared_ptr<CNode> node)
{
    return !(node->fInbound || node->m_manual_connection || node->fFeeler || node->fOneShot);
}

void PeerLogicValidation::InitializeNode(std::shared_ptr<CNode> pnode)
{
    std::string addrName = pnode->GetAddrName();
    NodeId nodeid = pnode->GetId();
    {
        LOCK(internal->cs_main);
        internal->mapNodeState.emplace_hint(internal->mapNodeState.end(),
                                            std::piecewise_construct,
                                            std::forward_as_tuple(nodeid),
                                            std::forward_as_tuple(std::move(addrName)));
    }
    if(!pnode->fInbound)
        internal->PushNodeVersion(pnode, connman, connman->timeData.GetTime());
}

void PeerLogicValidation::FinalizeNode(NodeId nodeid, bool& fUpdateConnectionTime)
{
    fUpdateConnectionTime = false;
    LOCK(internal->cs_main);
    CNodeState *state = internal->State(nodeid);
    assert(state != nullptr);

    if (state->nMisbehavior == 0 && state->fCurrentlyConnected)
        fUpdateConnectionTime = true;

    internal->mapNodeState.erase(nodeid);

    LogPrint(BCLog::NET, "Cleared nodestate for peer=%d\n", nodeid);
}

//////////////////////////////////////////////////////////////////////////////
//
// blockchain -> download logic notification
//

PeerLogicValidation::PeerLogicValidation(CConnman* connmanIn,
                                         bool enable_bip61)
    : connman(connmanIn)
    , logger_(connman->logger_)
    , internal(std::make_shared<PeerLogicValidation_internal>(logger_))
    , m_stale_tip_check_time(0)
    , m_enable_bip61(enable_bip61)
{
    // Stale tip checking and peer eviction are on two different timers, but we
    // don't want them to get out of sync due to drift in the scheduler, so we
    // combine them in one function and schedule at the quicker (peer-eviction)
    // timer.
    static_assert(EXTRA_PEER_CHECK_INTERVAL < STALE_CHECK_INTERVAL, "peer eviction timer should be less than stale tip check timer");
    connmanIn->scheduleEvery(std::bind(&PeerLogicValidation::CheckForStaleTipAndEvictPeers, this, N_POW_TARGET_SPACING),
                             EXTRA_PEER_CHECK_INTERVAL * 1000);
}

// All of the following cache a recent block, and are protected by cs_most_recent_block

//////////////////////////////////////////////////////////////////////////////
//
// Messages
//

void PeerLogicValidation_internal::RelayAddress(const CAddress& addr, bool fReachable, CConnman* connman)
{
    unsigned int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)

    // Relay to a limited number of other nodes
    // Use deterministic randomness to send to the same nodes for 24 hours
    // at a time so the addrKnowns of the chosen nodes prevent repeats
    uint64_t hashAddr = addr.GetHash();
    const CSipHasher hasher
            = connman->GetDeterministicRandomizer(RANDOMIZER_ID_ADDRESS_RELAY).Write(hashAddr << 32).Write((connman->timeData.GetTime()
            + hashAddr) / (24*60*60));
    FastRandomContext insecure_rand(connman->random_);

    std::array<std::pair<uint64_t, std::shared_ptr<CNode>>,2> best{{{0, nullptr}, {0, nullptr}}};
    assert(nRelayNodes <= best.size());

    auto sortfunc = [&best, &hasher, nRelayNodes](std::shared_ptr<CNode> pnode)
    {
        if (pnode->nVersion != 0)
        {
            uint64_t hashKey = CSipHasher(hasher).Write(pnode->GetId()).Finalize();
            for (unsigned int i = 0; i < nRelayNodes; i++)
            {
                if (hashKey > best[i].first)
                {
                    std::copy(best.begin() + i, best.begin() + nRelayNodes - 1, best.begin() + i + 1);
                    best[i] = std::make_pair(hashKey, pnode);
                    break;
                }
            }
        }
    };

    auto pushfunc = [&addr, &best, nRelayNodes, &insecure_rand]
    {
        for (unsigned int i = 0; i < nRelayNodes && best[i].first != 0; i++)
        {
            best[i].second->PushAddress(addr, insecure_rand);
        }
    };

    connman->ForEachNodeThen(std::move(sortfunc), std::move(pushfunc));
}

bool PeerLogicValidation_internal::ProcessMessage(std::shared_ptr<CNode> pfrom,
                                                  const std::string& strCommand,
                                                  CDataStream& vRecv,
                                                  int64_t nTimeReceived,
                                                  const CChainParams& chainparams,
                                                  CConnman* connman,
                                                  const std::atomic<bool>& interruptMsgProc,
                                                  bool enable_bip61)
{
    LogTrace(BCLog::NET, "received: %s (%u bytes) peer=%d\n", SanitizeString(strCommand), vRecv.size(), pfrom->GetId());
    if (connman->Args.IsArgSet("-dropmessagestest") && connman->random_.GetRand(connman->Args.GetArg("-dropmessagestest", 0)) == 0)
    {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (strCommand == NetMsgType::REJECT)
    {
        if (logger_.LogAcceptCategory(BCLog::NET))
        {
            try
            {
                std::string strMsg; unsigned char ccode; std::string strReason;
                vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE)
                        >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

                std::ostringstream ss;
                ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

                LogPrint(BCLog::NET, "Reject %s\n", SanitizeString(ss.str()));
            }
            catch (const std::ios_base::failure&)
            {
                // Avoid feedback loops by preventing reject messages from triggering a new reject message.
                LogPrint(BCLog::NET, "Unparseable reject message received\n");
            }
        }
        return true;
    }

    int banscore = connman->Args.GetArg("-banscore", DEFAULT_BANSCORE_THRESHOLD);

    if (strCommand == NetMsgType::VERSION)
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            if (enable_bip61) {
                connman->PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT,
                                                                                  strCommand,
                                                                                  REJECT_DUPLICATE,
                                                                                  std::string("Duplicate version message")));
            }
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 1, banscore);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        uint64_t nServiceInt;
        int nVersion;
        int nSendVersion;
        std::string strSubVer;
        std::string cleanSubVer;
        int nStartingHeight = -1;
        bool fRelay = true;

        vRecv >> nVersion >> nServiceInt >> nTime >> addrMe;
        nSendVersion = std::min(nVersion, PROTOCOL_VERSION);

        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty())
        {
            vRecv >> LIMITED_STRING(strSubVer, MAX_SUBVERSION_LENGTH);
            cleanSubVer = SanitizeString(strSubVer);
        }
        if (!vRecv.empty())
        {
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
            connman->SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            PushNodeVersion(pfrom, connman, connman->timeData.GetAdjustedTime());

        connman->PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERACK));

        pfrom->SetAddrLocal(addrMe);
        {
            LOCK(pfrom->cs_SubVer);
            pfrom->strSubVer = strSubVer;
            pfrom->cleanSubVer = cleanSubVer;
        }

        // Change version
        pfrom->SetSendVersion(nSendVersion);
        pfrom->nVersion = nVersion;

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (connman->fListen)
            {
                CAddress addr = connman->GetLocalAddress(&pfrom->addr);
                FastRandomContext insecure_rand(connman->random_);
                if (addr.IsRoutable())
                {
                    LogPrint(BCLog::NET, "ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr, insecure_rand);
                }
                else if (connman->IsPeerAddrLocalGood(pfrom))
                {
                    addr.SetIP(addrMe);
                    LogPrint(BCLog::NET, "ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr, insecure_rand);
                }
            }

            // Get recent addresses
            connman->PushMessage(pfrom, CNetMsgMaker(nSendVersion).Make(NetMsgType::GETADDR));
            pfrom->fGetAddr = true;
            connman->MarkAddressGood(pfrom->addr);
        }

        std::string remoteAddr;
        if (connman->fLogIPs)
            remoteAddr = ", peeraddr=" + pfrom->addr.ToString();

        LogPrint(BCLog::NET, "receive version message: %s: version %d, us=%s, peer=%d%s\n",
                 cleanSubVer, pfrom->nVersion,
                 addrMe.ToString(), pfrom->GetId(),
                 remoteAddr);

        int64_t nTimeOffset = nTime - connman->timeData.GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        connman->timeData.AddTimeData(connman->Args, *connman->clientInterface, pfrom->addr, nTimeOffset);

        // Feeler connections exist only to verify if address is online.
        if (pfrom->fFeeler)
        {
            assert(pfrom->fInbound == false);
            pfrom->fDisconnect = true;
        }
        return true;
    }

    if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        LOCK(cs_main);
        Misbehaving(pfrom->GetId(), 1, banscore);
        return false;
    }

    // At this point, the outgoing message serialization version can't change.
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());

    if (strCommand == NetMsgType::VERACK)
    {
        pfrom->SetRecvVersion(std::min(pfrom->nVersion.load(), PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Mark this node as currently connected, so we update its timestamp later.
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
            LogPrintf("New outbound peer connected: version: %d, peer=%d%s\n",
                      pfrom->nVersion.load(), pfrom->GetId(),
                      (connman->fLogIPs ? strprintf(", peeraddr=%s", pfrom->addr.ToString()) : ""));
        }

        pfrom->fSuccessfullyConnected = true;
        return true;
    }

    if (!pfrom->fSuccessfullyConnected)
    {
        // Must have a verack message before anything else
        LOCK(cs_main);
        Misbehaving(pfrom->GetId(), 1, banscore);
        return false;
    }

    if (strCommand == NetMsgType::ADDR)
    {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        if (vAddr.size() > 1000)
        {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20, banscore, strprintf("message addr size() = %u", vAddr.size()));
            return false;
        }

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = connman->timeData.GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for (CAddress& addr : vAddr)
        {
            if (interruptMsgProc)
                return true;

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = connman->IsReachable(addr);
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

    if (strCommand == NetMsgType::PROPAGATE)
    {
        LOCK(cs_main);
        std::vector<uint8_t> mess;
        vRecv >> mess;
        if (connman->p2p->PropagateMessage(&mess[0], mess.size(), false))
        {
            CNodeState *nodestate = State(pfrom->GetId());
            nodestate->m_last_block_announcement = connman->timeData.GetTime();
        }
        return true;
    }

    if (strCommand == NetMsgType::GETADDR)
    {
        // This asymmetric behavior for inbound and outbound connections was introduced
        // to prevent a fingerprinting attack: an attacker can send specific fake addresses
        // to users' AddrMan and later request them by sending getaddr messages.
        // Making nodes which are behind NAT and can only make outgoing connections ignore
        // the getaddr message mitigates the attack.
        if (!pfrom->fInbound)
        {
            LogPrint(BCLog::NET, "Ignoring \"getaddr\" from outbound connection. peer=%d\n", pfrom->GetId());
            return true;
        }

        // Only send one GetAddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        if (pfrom->fSentAddr)
        {
            LogPrint(BCLog::NET, "Ignoring repeated \"getaddr\". peer=%d\n", pfrom->GetId());
            return true;
        }
        pfrom->fSentAddr = true;

        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vAddr = connman->GetAddresses();
        FastRandomContext insecure_rand(connman->random_);
        for (const CAddress &addr : vAddr)
            pfrom->PushAddress(addr, insecure_rand);
        return true;
    }

    if (strCommand == NetMsgType::PING)
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
        return true;
    }

    if (strCommand == NetMsgType::PONG)
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce))
        {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0)
            {
                if (nonce == pfrom->nPingNonceSent)
                {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0)
                    {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime.load(), pingUsecTime);
                    }
                    else
                    {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                }
                else
                {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0)
                    {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            }
            else
            {
                sProblem = "Unsolicited pong without ping";
            }
        }
        else
        {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty()))
        {
            LogPrint(BCLog::NET, "pong peer=%d: %s, %x expected, %x received, %u bytes\n",
                     pfrom->GetId(),
                     sProblem,
                     pfrom->nPingNonceSent,
                     nonce,
                     nAvail);
        }
        if (bPingFinished)
        {
            pfrom->nPingNonceSent = 0;
        }
        return true;
    }

    // Ignore unknown commands for extensibility
    LogPrint(BCLog::NET, "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->GetId());
    return true;
}

bool PeerLogicValidation_internal::SendRejectsAndCheckIfBanned(std::shared_ptr<CNode> pnode,
                                                               CConnman* connman,
                                                               bool enable_bip61) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);
    CNodeState &state = *State(pnode->GetId());

    if (state.fShouldBan)
    {
        state.fShouldBan = false;
        if (pnode->fWhitelisted)
            LogPrintf("Warning: not punishing whitelisted peer %s!\n", pnode->addr.ToString());
        else if (pnode->m_manual_connection)
            LogPrintf("Warning: not punishing manually-connected peer %s!\n", pnode->addr.ToString());
        else
        {
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

bool PeerLogicValidation::ProcessMessages(std::shared_ptr<CNode> pfrom, std::atomic<bool>& interruptMsgProc)
{
    const CChainParams& chainparams = connman->Params();
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
        bool oldfPauseRecv = pfrom->fPauseRecv;
        pfrom->fPauseRecv = pfrom->nProcessQueueSize > connman->GetReceiveFloodSize();
        if (oldfPauseRecv && !pfrom->fPauseRecv)
        {
            pfrom->session->start();
        }
        fMoreWork = !pfrom->vProcessMsg.empty();
    }
    CNetMessage& msg(msgs.front());

    msg.SetVersion(pfrom->GetRecvVersion());
    // Scan for message start
    if (memcmp(msg.hdr.pchMessageStart, chainparams.MessageStart(), CMessageHeader::MESSAGE_START_SIZE) != 0)
    {
        LogPrint(BCLog::NET, "PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n", SanitizeString(msg.hdr.GetCommand()), pfrom->GetId());
        pfrom->fDisconnect = true;
        return false;
    }

    // Read header
    CMessageHeader& hdr = msg.hdr;
    if (!hdr.IsValid(chainparams.MessageStart(), logger_))
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
        fRet = internal->ProcessMessage(pfrom, strCommand, vRecv, msg.nTime, chainparams, connman, interruptMsgProc, m_enable_bip61);
        if (interruptMsgProc)
            return false;
    }
    catch (const std::ios_base::failure& e)
    {
        if (m_enable_bip61)
        {
            connman->PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT,
                                                                              strCommand,
                                                                              REJECT_MALFORMED,
                                                                              std::string("error parsing message")));
        }
        if (strstr(e.what(), "end of data"))
        {
            // Allow exceptions from under-length message on vRecv
            LogPrint(BCLog::NET, "%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter "
                     "than its stated length\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
        }
        else if (strstr(e.what(), "size too large"))
        {
            // Allow exceptions from over-long size
            LogPrint(BCLog::NET, "%s(%s, %u bytes): Exception '%s' caught\n",
                     __func__, SanitizeString(strCommand), nMessageSize, e.what());
        }
        else if (strstr(e.what(), "non-canonical ReadCompactSize()"))
        {
            // Allow exceptions from non-canonical encoding
            LogPrint(BCLog::NET, "%s(%s, %u bytes): Exception '%s' caught\n",
                     __func__, SanitizeString(strCommand), nMessageSize, e.what());
        }
        else
        {
            PrintExceptionContinue(logger_, &e, "ProcessMessages()");
        }
    }
    catch (const std::exception& e)
    {
        PrintExceptionContinue(logger_, &e, "ProcessMessages()");
    }
    catch (...)
    {
        PrintExceptionContinue(logger_, nullptr, "ProcessMessages()");
    }

    if (!fRet)
    {
        LogPrint(BCLog::NET, "%s(%s, %u bytes) FAILED peer=%d\n", __func__, SanitizeString(strCommand), nMessageSize, pfrom->GetId());
    }

    LOCK(internal->cs_main);
    internal->SendRejectsAndCheckIfBanned(pfrom, connman, m_enable_bip61);

    return fMoreWork;
}

void PeerLogicValidation::EvictExtraOutboundPeers(int64_t time_in_seconds)
{
    // Check whether we have too many outbound peers
    int extra_peers = connman->GetExtraOutboundCount();
    if (extra_peers > 0)
    {
        // If we have more outbound peers than we target, disconnect one.
        // Pick the outbound peer that least recently announced
        // us a new block, with ties broken by choosing the more recent
        // connection (higher node id)
        NodeId worst_peer = -1;
        int64_t oldest_block_announcement = std::numeric_limits<int64_t>::max();

        LOCK(internal->cs_main);

        connman->ForEachNode([&](std::shared_ptr<CNode> pnode)
        {
            AssertLockHeld(internal->cs_main);

            // Ignore non-outbound peers, or nodes marked for disconnect already
            if (!IsOutboundDisconnectionCandidate(pnode) || pnode->fDisconnect) return;
            CNodeState *state = internal->State(pnode->GetId());
            if (state == nullptr) return; // shouldn't be possible, but just in case
            if (state->m_last_block_announcement < oldest_block_announcement
                    || (state->m_last_block_announcement == oldest_block_announcement && pnode->GetId() > worst_peer))
            {
                worst_peer = pnode->GetId();
                oldest_block_announcement = state->m_last_block_announcement;
            }
        });
        if (worst_peer != -1)
        {
            bool disconnected = connman->ForNode(worst_peer, [&](std::shared_ptr<CNode> pnode)
            {
                AssertLockHeld(internal->cs_main);

                // Only disconnect a peer that has been connected to us for
                // some reasonable fraction of our check-frequency, to give
                // it time for new information to have arrived.
                // Also don't disconnect any peer we're trying to download a
                // block from.
                if (time_in_seconds - pnode->nTimeConnected > MINIMUM_CONNECT_TIME)
                {
                    LogPrint(BCLog::NET, "disconnecting extra outbound peer=%d (last block announcement received at time %d)\n",
                             pnode->GetId(), oldest_block_announcement);
                    pnode->fDisconnect = true;
                    return true;
                }
                else
                {
                    LogPrint(BCLog::NET, "keeping outbound peer=%d chosen for eviction (connect time: %d)\n",
                             pnode->GetId(), pnode->nTimeConnected);
                    return false;
                }
            });
            if (disconnected)
            {
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

void PeerLogicValidation::CheckForStaleTipAndEvictPeers(int nPowTargetSpacing)
{
    LogTrace(BCLog::NET, "Called CheckForStaleTipAndEvictPeers(%d)\n", nPowTargetSpacing);
    if (connman == nullptr) return;

    int64_t time_in_seconds = connman->timeData.GetTime();

    EvictExtraOutboundPeers(time_in_seconds);

    if (time_in_seconds > m_stale_tip_check_time)
    {
        LOCK(internal->cs_main);
        // Check whether our tip is stale, and if so, allow using an extra
        // outbound peer
        if (TipMayBeStale(nPowTargetSpacing))
        {
            LogPrintf("Potential stale tip detected, will try using extra outbound peer "
                      "(last tip update: %d seconds ago)\n", time_in_seconds - internal->g_last_tip_update);
            connman->SetTryNewOutboundPeer(true);
        }
        else if (connman->GetTryNewOutboundPeer())
        {
            connman->SetTryNewOutboundPeer(false);
        }
        m_stale_tip_check_time = time_in_seconds + STALE_CHECK_INTERVAL;
    }
}

bool PeerLogicValidation::SendMessages(std::shared_ptr<CNode> pto)
{
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
        if (pto->fPingQueued)
        {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros())
        {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend)
        {
            uint64_t nonce = 0;
            while (nonce == 0)
            {
                connman->random_.GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            pto->nPingNonceSent = nonce;
            connman->PushMessage(pto, msgMaker.Make(NetMsgType::PING, nonce));
        }

        TRY_LOCK(internal->cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
            return true;

        if (internal->SendRejectsAndCheckIfBanned(pto, connman, m_enable_bip61))
            return true;
        CNodeState &state = *internal->State(pto->GetId());

        // Address refresh broadcast
        int64_t nNow = GetTimeMicros();
        if (pto->nNextLocalAddrSend < nNow)
        {
            connman->AdvertiseLocal(pto);
            pto->nNextLocalAddrSend = connman->PoissonNextSend(nNow, AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL);
        }

        //
        // Message: addr
        //
        if (pto->nNextAddrSend < nNow)
        {
            pto->nNextAddrSend = connman->PoissonNextSend(nNow, AVG_ADDRESS_BROADCAST_INTERVAL);
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
        /* Do not send propagate messages if send queue is too long */
        if (pto->fPauseSend)
            return true;
        const PropagateMessage *promess = connman->p2p_store->GetNext(pto->next_propagate_index);
        if (promess)
        {

            LogPrintf("PeerLogicValidation_internal::SendMessage-promess->hash=%s,peer=%s",promess->hash.ToString(),pto->addr.ToString());
            connman->PushMessage(pto, msgMaker.Make(NetMsgType::PROPAGATE, promess->message));
        }
    }
    return true;
}

