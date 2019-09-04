// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fcntl.h>
#include <math.h>
#include <logos/lib/log.hpp>
#include <logos/lib/trace.hpp>
#include <config/bitcoin-config.h>
#include <net.h>
#include <chainparams.h>
#include <crypto/common.h>
#include <netbase.h>
#include <ui_interface.h>
#include <utilstrencodings.h>

// Dump addresses to peers.dat and banlist.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

// We add a random period time (0 to 1 seconds) to feeler connections to prevent synchronization.
#define FEELER_SLEEP_WINDOW 1

/** Used to pass flags to the Bind() function */
enum BindFlags
{
    BF_NONE         = 0,
    BF_EXPLICIT     = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST    = (1U << 2),
};

const static std::string NET_MESSAGE_COMMAND_OTHER = "*other*";

constexpr uint64_t RANDOMIZER_ID_NETGROUP = 0x6c0edd8036ef4036ULL; // SHA256("netgroup")[0:8]
constexpr uint64_t RANDOMIZER_ID_LOCALHOSTNONCE = 0xd93e69e2bbfa5735ULL; // SHA256("localhostnonce")[0:8]

void CConnman::AddOneShot(const std::string& strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

unsigned short CConnman::GetListenPort()
{
    return (unsigned short)(Args.GetArg("-port", Params().GetDefaultPort()));
}

// find 'best' local address for a particular peer
bool CConnman::GetLocal(CService& addr, const CNetAddr *paddrPeer)
{
    if (!fListen)
        return false;

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        LOCK(cs_mapLocalHost);
        for (const auto& entry : mapLocalHost)
        {
            int nScore = entry.second.nScore;
            int nReachability = entry.first.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore))
            {
                addr = CService(entry.first, entry.second.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

//! Convert the pnSeed6 array into usable address objects.
std::vector<CAddress> CConnman::convertSeed6(const std::vector<SeedSpec6> &vSeedsIn)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    std::vector<CAddress> vSeedsOut;
    vSeedsOut.reserve(vSeedsIn.size());
    for (const auto& seed_in : vSeedsIn)
    {
        struct in6_addr ip;
        memcpy(&ip, seed_in.addr, sizeof(ip));
        CAddress addr(CService(ip, seed_in.port));
        addr.nTime = timeData.GetTime() - random_.GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
    return vSeedsOut;
}

// get best local address for a particular peer as a CAddress
// Otherwise, return the unroutable 0.0.0.0 but filled in with
// the normal parameters, since the IP may be changed to a useful
// one by discovery.
CAddress CConnman::GetLocalAddress(const CNetAddr *paddrPeer)
{
    CAddress ret(CService(CNetAddr(),GetListenPort()));
    CService addr;
    if (GetLocal(addr, paddrPeer))
    {
        ret = CAddress(addr);
    }
    ret.nTime = timeData.GetAdjustedTime();
    return ret;
}

int CConnman::GetnScore(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    if (mapLocalHost.count(addr) == LOCAL_NONE)
        return 0;
    return mapLocalHost[addr].nScore;
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool CConnman::IsPeerAddrLocalGood(std::shared_ptr<CNode> pnode)
{
    CService addrLocal = pnode->GetAddrLocal();
    return fDiscover && pnode->addr.IsRoutable() && addrLocal.IsRoutable() &&
            !IsLimited(addrLocal.GetNetwork());
}

// pushes our own address to a peer
void CConnman::AdvertiseLocal(std::shared_ptr<CNode> pnode)
{
    if (fListen && pnode->fSuccessfullyConnected)
    {
        CAddress addrLocal = GetLocalAddress(&pnode->addr);
        if (Args.GetBoolArg("-addrmantest", false))
        {
            // use IPv4 loopback during addrmantest
            addrLocal = CAddress(CService(LookupNumeric("127.0.0.1", GetListenPort())));
        }
        // If discovery is enabled, sometimes give our peer the address it
        // tells us that it sees us as in case it has a better idea of our
        // address than we do.
        if (IsPeerAddrLocalGood(pnode) && (!addrLocal.IsRoutable() ||
                                           random_.GetRand((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8:2) == 0))
        {
            addrLocal.SetIP(pnode->GetAddrLocal());
        }
        if (addrLocal.IsRoutable() || Args.GetBoolArg("-addrmantest", false))
        {
            LogPrint(BCLog::NET, "AdvertiseLocal: advertising address %s\n", addrLocal.ToString());
            FastRandomContext insecure_rand(random_);
            pnode->PushAddress(addrLocal, insecure_rand);
        }
    }
}

// learn a new local address
bool CConnman::AddLocal(const CService& addr, int nScore)
{
    if (!addr.IsRoutable())
        return false;

    if (!fDiscover && nScore < LOCAL_MANUAL)
        return false;

    if (IsLimited(addr))
        return false;

    LogPrintf("AddLocal(%s,%i)\n", addr.ToString(), nScore);

    {
        LOCK(cs_mapLocalHost);
        bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore)
        {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
    }

    return true;
}

bool CConnman::AddLocal(const CNetAddr &addr, int nScore)
{
    return AddLocal(CService(addr, GetListenPort()), nScore);
}

void CConnman::RemoveLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    LogPrintf("RemoveLocal(%s)\n", addr.ToString());
    mapLocalHost.erase(addr);
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void CConnman::SetLimited(enum Network net, bool fLimited)
{
    if (net == NET_UNROUTABLE || net == NET_INTERNAL)
        return;
    LOCK(cs_mapLocalHost);
    vfLimited[net] = fLimited;
}

bool CConnman::IsLimited(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return vfLimited[net];
}

bool CConnman::IsLimited(const CNetAddr &addr)
{
    return IsLimited(addr.GetNetwork());
}

/** vote for a local address */
bool CConnman::SeenLocal(const CService& addr)
{
    {
        LOCK(cs_mapLocalHost);
        if (mapLocalHost.count(addr) == 0)
            return false;
        mapLocalHost[addr].nScore++;
    }
    return true;
}


/** check whether a given address is potentially local */
bool CConnman::IsLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    return mapLocalHost.count(addr) > 0;

}

/** check whether a given network is one we can probably connect to */
bool CConnman::IsReachable(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return !vfLimited[net];
}

/** check whether a given address is in a network we can probably connect to */
bool CConnman::IsReachable(const CNetAddr& addr)
{
    enum Network net = addr.GetNetwork();
    return IsReachable(net);
}

std::shared_ptr<CNode> CConnman::FindNode(const CNetAddr& ip)
{
    LOCK(cs_vNodes);
    for (auto&& pnode : vNodes)
    {
        if (static_cast<CNetAddr>(pnode->addr) == ip)
            return pnode;
    }
    return std::shared_ptr<CNode>();
}

std::shared_ptr<CNode> CConnman::FindNode(const CSubNet& subNet)
{
    LOCK(cs_vNodes);
    for (auto&& pnode : vNodes)
    {
        if (subNet.Match(static_cast<CNetAddr>(pnode->addr)))
            return pnode;
    }
    return std::shared_ptr<CNode>();
}

std::shared_ptr<CNode> CConnman::FindNode(const std::string& addrName)
{
    LOCK(cs_vNodes);
    for (auto&& pnode : vNodes)
    {
        if (pnode->GetAddrName() == addrName)
            return pnode;
    }
    return std::shared_ptr<CNode>();
}

std::shared_ptr<CNode> CConnman::FindNode(const CService& addr)
{
    LOCK(cs_vNodes);
    for (auto&& pnode : vNodes)
    {
        if (static_cast<CService>(pnode->addr) == addr)
            return pnode;
    }
    return std::shared_ptr<CNode>();
}

bool CConnman::CheckIncomingNonce(uint64_t nonce)
{
    LOCK(cs_vNodes);
    for (auto&& pnode : vNodes)
    {
        if (!pnode->fSuccessfullyConnected && !pnode->fInbound && pnode->GetLocalNonce() == nonce)
            return false;
    }
    return true;
}

AsioSession::AsioSession(boost::asio::io_service& ios,
                         CConnman &connman_)
    : p2p(connman_.p2p->p2p)
    , connman(connman_)
    , socket(ios)
    , pnode(0)
    , id(-1ll)
    , read_running(false)
    , in_shutdown(false)
    , logger_(connman.logger_)
{
    LogDebug(BCLog::NET, "Session created, this=%p", this);
}

AsioSession::~AsioSession()
{
    LogDebug(BCLog::NET, "Session removed, this=%p, peer=%lld", this, id);
}

void AsioSession::setNode(std::shared_ptr<CNode> pnode_)
{
    if (pnode)
    {
        LogDebug(BCLog::NET, "Double node set, peer=%ld\n", id);
        return;
    }
    pnode = pnode_;
    id = pnode->id;
}

void AsioSession::start()
{
    // debug socket number to track file descriptor leaks
    bool expected = false;
    if (read_running.compare_exchange_weak(expected, true))
    {
        LogDebug(BCLog::NET, "Session reading started, this=%p, socket=%d, peer=%lld",
                this, socket.native_handle(), id);
        socket.async_read_some(boost::asio::buffer(data, max_length),
                           boost::bind(&AsioSession::handle_read, this, shared_from_this(),
                                       boost::asio::placeholders::error,
                                       boost::asio::placeholders::bytes_transferred));
    }
}

void AsioSession::shutdown()
{
    bool expected = false;
    if (!in_shutdown.compare_exchange_weak(expected, true))
    {
        LogDebug(BCLog::NET, "Double session shutdown ignored, peer=%ld\n", id);
        return;
    }
    boost::system::error_code error;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
    if (error)
    {
        LogError(BCLog::NET, "Error in session shutdown, peer=%ld: %s\n", id, error.message());
    }
    else
    {
        LogDebug(BCLog::NET, "Session shutdown, peer=%ld\n", id);
    }
    pnode = std::shared_ptr<CNode>();
}

void AsioSession::handle_read(std::shared_ptr<AsioSession> s,
                              const boost::system::error_code& err,
                              size_t bytes_transferred)
{
    LogTrace(BCLog::NET, "Session handle_read called after transmission of %lld bytes, peer=%lld", bytes_transferred, id);
    if (err)
    {
        LogError(BCLog::NET, "Error in receive, peer=%lld: %s", id, err.message());
        if (!in_shutdown)
            connman.AcceptReceivedBytes(pnode, data, -1);
        shutdown();
    }
    else if (in_shutdown)
    {
        LogWarning(BCLog::NET, "Received %d bytes before shutdown, peer=%lld", bytes_transferred, id);
        shutdown();
    }
    else if (!connman.AcceptReceivedBytes(pnode, data, bytes_transferred))
    {
        LogError(BCLog::NET, "Error in accept %d received bytes, peer=%lld", bytes_transferred, id);
        shutdown();
    }
    else
    {
        LogTrace(BCLog::NET, "Received %ld bytes, peer=%lld", bytes_transferred, id);
        auto pnode_ = pnode;
        if (!pnode_ || !pnode_->fPauseRecv)
        {
            socket.async_read_some(boost::asio::buffer(data, max_length),
                               boost::bind(&AsioSession::handle_read, this, shared_from_this(),
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred));
        }
        else
        {
            LogDebug(BCLog::NET, "Session reading stopped, this=%p, socket=%d, peer=%lld",
                    this, socket.native_handle(), id);
            read_running = false;
        }
    }
}

void AsioSession::handle_write(std::shared_ptr<AsioSession> s,
                               const boost::system::error_code& err,
                               size_t bytes_transferred)
{
    LogTrace(BCLog::NET, "Session handle_write called after transmission of %lld bytes, peer=%lld", bytes_transferred, id);
    if (err)
    {
        LogError(BCLog::NET, "Error in transmit, peer=%lld: %s", id, err.message());
        if (!in_shutdown)
        {
            connman.SocketSendFinish(pnode, -1);
            shutdown();
        }
    }
    else if (in_shutdown)
    {
        LogWarning(BCLog::NET, "Transmitted %d bytes before shutdown, peer=%lld", bytes_transferred, id);
    }
    else if (!connman.SocketSendFinish(pnode, bytes_transferred))
    {
        LogError(BCLog::NET, "Error in accept %d transmitted bytes, peer=%lld", bytes_transferred, id);
        shutdown();
    }
    else
    {
        LogTrace(BCLog::NET, "Transmitted %d bytes, peer=%lld", bytes_transferred, id);
        sem_post(&connman.dataWritten);
    }
}

void AsioSession::async_write(const char *buf, size_t bytes)
{
    boost::asio::async_write(socket, boost::asio::buffer(buf, bytes),
                             boost::bind(&AsioSession::handle_write, this, shared_from_this(),
                                         boost::asio::placeholders::error,
                                         boost::asio::placeholders::bytes_transferred));
}

AsioClient::AsioClient(CConnman &conn,
                       const char *nam,
                       std::shared_ptr<CSemaphoreGrant> grant,
                       int fl)
    : p2p(conn.p2p->p2p)
    , connman(conn)
    , name(nam ? strdup(nam) : nullptr)
    , grantOutbound(grant)
    , flags(fl)
    , resolver(*conn.io_service)
    , logger_(conn.logger_)
{
}

AsioClient::~AsioClient()
{
    if (name)
        free(name);
}

void AsioClient::connect(const std::string &host, const std::string &port)
{
    resolver.async_resolve(host, port, boost::bind(&AsioClient::resolve_handler, this, _1, _2));
}

void AsioClient::resolve_handler(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::results_type results)
{
    if (ec)
    {
        LogWarning(BCLog::NET, "Resolve error: %s", ec.message());
    }
    else
    {
        std::shared_ptr<AsioSession> session = std::make_shared<AsioSession>(*connman.io_service, connman);
        boost::asio::async_connect(session->get_socket(), results,
                                   boost::bind(&AsioClient::connect_handler, this, session, _1, _2));
    }
}

std::shared_ptr<CNode> CConnman::ConnectNodeFinish(AsioClient *client, std::shared_ptr<AsioSession> session)
{
    boost::system::error_code ec;
    boost::asio::ip::tcp::endpoint endpoint = session->get_socket().remote_endpoint(ec);
    if (ec)
    {
        Log log;
        LOG_FATAL(log) << "CConnman::ConnectNodeFinish - error retrieving remote endpoint with code: " << ec.message();
        trace_and_halt();
    }
    CService saddr = LookupNumeric(endpoint.address().to_string().c_str(), endpoint.port());
    CAddress addr(saddr);

    // It is possible that we already have a connection to the IP/port resolved to.
    // In that case, drop the connection that was just created, and return the existing CNode instead.
    // Also store the name we used to connect in that CNode, so that future FindNode() calls to that
    // name catch this early.
    if (client->name)
    {
        LOCK(cs_vNodes);
        std::shared_ptr<CNode> pnode = FindNode(saddr);
        if (pnode)
        {
            pnode->MaybeSetAddrName(std::string(client->name));
            LogInfo(BCLog::NET, "Failed to open new connection, already connected\n");
            return std::shared_ptr<CNode>();
        }
    }

    endpoint = session->get_socket().local_endpoint();
    CService saddr_bind = LookupNumeric(endpoint.address().to_string().c_str(), endpoint.port());
    CAddress addr_bind(saddr_bind);

    // Add node
    NodeId id = GetNewNodeId();
    uint64_t nonce = GetDeterministicRandomizer(RANDOMIZER_ID_LOCALHOSTNONCE).Write(id).Finalize();
    std::shared_ptr<CNode> pnode = std::make_shared<CNode>(id, session, addr, CalculateKeyedNetGroup(addr),
                                                           nonce, addr_bind, client->name ? client->name : "", false);
    session->setNode(pnode);

    if (client->grantOutbound)
        client->grantOutbound->MoveTo(pnode->grantOutbound);
    if (client->flags & CONN_ONE_SHOT)
        pnode->fOneShot = true;
    if (client->flags & CONN_FEELER)
        pnode->fFeeler = true;
    if (client->flags & CONN_MANUAL)
        pnode->m_manual_connection = true;

    LogInfo(BCLog::NET, "connection to %s (%s) established\n", (client->name ? client->name : ""),
            saddr.ToString().c_str());

    m_msgproc->InitializeNode(pnode);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(pnode);
    }
    return pnode;
}

void AsioClient::connect_handler(std::shared_ptr<AsioSession> session, const boost::system::error_code& ec,
                                 const boost::asio::ip::tcp::endpoint& endpoint)
{
    if (ec)
    {
        LogWarning(BCLog::NET, "Connect error: %s", ec.message());
    }
    else if (!connman.ConnectNodeFinish(this, session))
    {
        LogInfo(BCLog::NET, "Connected node already exists");
    }
    else
    {
        session->start();
    }
    delete this;
}

void CConnman::ConnectNode(CAddress addrConnect, const char *pszDest, std::shared_ptr<CSemaphoreGrant> grantOutbound, int flags)
{
    if (pszDest == nullptr)
    {
        if (IsLocal(addrConnect))
            return;

        // Look for an existing connection
        std::shared_ptr<CNode> pnode = FindNode(static_cast<CService>(addrConnect));
        if (pnode)
        {
            LogPrintf("Failed to open new connection, already connected\n");
            return;
        }
    }

    /// debug print
    LogPrint(BCLog::NET, "trying connection %s lastseen=%.1fhrs\n",
             pszDest ? pszDest : addrConnect.ToString(),
             pszDest ? 0.0 : (double)(timeData.GetAdjustedTime() - addrConnect.nTime)/3600.0);

    AsioClient *client = new AsioClient(*this, pszDest, grantOutbound, flags);
    std::string host, port;

    if (pszDest)
    {
        int default_port = Params().GetDefaultPort();
        SplitHostPort(std::string(pszDest), default_port, host);
        port = std::to_string(default_port);
    }
    else
    {
        host = addrConnect.ToStringIP();
        port = addrConnect.ToStringPort();
    }

    client->connect(host, port);
}

void CConnman::DumpBanlist()
{
    SweepBanned(); // clean unused entries (if bantime has expired)

    if (!BannedSetIsDirty())
        return;

    int64_t nStart = GetTimeMillis();

    CBanDB bandb(config, logger_, chainParams);
    banmap_t banmap;
    GetBanned(banmap);
    if (bandb.Write(banmap))
        SetBannedSetDirty(false);

    LogPrint(BCLog::NET, "Flushed %d banned node ips/subnets to banlist.dat  %dms\n",
             banmap.size(), GetTimeMillis() - nStart);
}

void CNode::CloseSocketDisconnect()
{
    fDisconnect = true;
    if (session)
    {
        session->shutdown();
    }
}

void CConnman::ClearBanned()
{
    {
        LOCK(cs_setBanned);
        setBanned.clear();
        setBannedIsDirty = true;
    }
    DumpBanlist(); //store banlist to disk
    if(clientInterface)
        clientInterface->BannedListChanged();
}

bool CConnman::IsBanned(CNetAddr ip)
{
    LOCK(cs_setBanned);
    for (const auto& it : setBanned)
    {
        CSubNet subNet = it.first;
        CBanEntry banEntry = it.second;

        if (subNet.Match(ip) && timeData.GetTime() < banEntry.nBanUntil)
            return true;
    }
    return false;
}

bool CConnman::IsBanned(CSubNet subnet)
{
    LOCK(cs_setBanned);
    banmap_t::iterator i = setBanned.find(subnet);
    if (i != setBanned.end())
    {
        CBanEntry banEntry = (*i).second;
        if (timeData.GetTime() < banEntry.nBanUntil)
            return true;
    }
    return false;
}

void CConnman::Ban(const CNetAddr& addr, const BanReason &banReason, int64_t bantimeoffset, bool sinceUnixEpoch)
{
    CSubNet subNet(addr);
    Ban(subNet, banReason, bantimeoffset, sinceUnixEpoch);
}

void CConnman::Ban(const CSubNet& subNet, const BanReason &banReason, int64_t bantimeoffset, bool sinceUnixEpoch)
{
    CBanEntry banEntry(timeData.GetTime());
    banEntry.banReason = banReason;
    if (bantimeoffset <= 0)
    {
        bantimeoffset = Args.GetArg("-bantime", DEFAULT_MISBEHAVING_BANTIME);
        sinceUnixEpoch = false;
    }
    banEntry.nBanUntil = (sinceUnixEpoch ? 0 : timeData.GetTime() )+bantimeoffset;

    {
        LOCK(cs_setBanned);
        if (setBanned[subNet].nBanUntil < banEntry.nBanUntil)
        {
            setBanned[subNet] = banEntry;
            setBannedIsDirty = true;
        }
        else
            return;
    }
    if(clientInterface)
        clientInterface->BannedListChanged();
    {
        LOCK(cs_vNodes);
        for (auto&& pnode : vNodes)
        {
            if (subNet.Match(static_cast<CNetAddr>(pnode->addr)))
                pnode->fDisconnect = true;
        }
    }
    if(banReason == BanReasonManuallyAdded)
        DumpBanlist(); //store banlist to disk immediately if user requested ban
}

bool CConnman::Unban(const CNetAddr &addr)
{
    CSubNet subNet(addr);
    return Unban(subNet);
}

bool CConnman::Unban(const CSubNet &subNet)
{
    {
        LOCK(cs_setBanned);
        if (!setBanned.erase(subNet))
            return false;
        setBannedIsDirty = true;
    }
    if(clientInterface)
        clientInterface->BannedListChanged();
    DumpBanlist(); //store banlist to disk immediately
    return true;
}

void CConnman::GetBanned(banmap_t &banMap)
{
    LOCK(cs_setBanned);
    // Sweep the banlist so expired bans are not returned
    SweepBanned();
    banMap = setBanned; //create a thread safe copy
}

void CConnman::SetBanned(const banmap_t &banMap)
{
    LOCK(cs_setBanned);
    setBanned = banMap;
    setBannedIsDirty = true;
}

void CConnman::SweepBanned()
{
    int64_t now = timeData.GetTime();
    bool notifyUI = false;
    {
        LOCK(cs_setBanned);
        banmap_t::iterator it = setBanned.begin();
        while(it != setBanned.end())
        {
            CSubNet subNet = (*it).first;
            CBanEntry banEntry = (*it).second;
            if(now > banEntry.nBanUntil)
            {
                setBanned.erase(it++);
                setBannedIsDirty = true;
                notifyUI = true;
                LogPrint(BCLog::NET, "%s: Removed banned node ip/subnet from banlist.dat: %s\n", __func__, subNet.ToString());
            }
            else
                ++it;
        }
    }
    // update UI
    if(notifyUI && clientInterface)
        clientInterface->BannedListChanged();
}

bool CConnman::BannedSetIsDirty()
{
    LOCK(cs_setBanned);
    return setBannedIsDirty;
}

void CConnman::SetBannedSetDirty(bool dirty)
{
    LOCK(cs_setBanned); //reuse setBanned lock for the isDirty flag
    setBannedIsDirty = dirty;
}


bool CConnman::IsWhitelistedRange(const CNetAddr &addr)
{
    for (const CSubNet& subnet : vWhitelistedRange)
    {
        if (subnet.Match(addr))
            return true;
    }
    return false;
}

std::string CNode::GetAddrName() const
{
    LOCK(cs_addrName);
    return addrName;
}

void CNode::MaybeSetAddrName(const std::string& addrNameIn)
{
    LOCK(cs_addrName);
    if (addrName.empty())
        addrName = addrNameIn;
}

CService CNode::GetAddrLocal() const
{
    LOCK(cs_addrLocal);
    return addrLocal;
}

void CNode::SetAddrLocal(const CService& addrLocalIn)
{
    LOCK(cs_addrLocal);
    if (addrLocal.IsValid())
        error(logger_, "Addr local already set for node: %i. Refusing to change from %s to %s",
              id, addrLocal.ToString(), addrLocalIn.ToString());
    else
        addrLocal = addrLocalIn;
}

bool CNode::ReceiveMsgBytes(const char *pch, unsigned int nBytes, bool& complete)
{
    complete = false;
    int64_t nTimeMicros = GetTimeMicros();
    LOCK(cs_vRecv);
    nLastRecv = nTimeMicros / 1000000;
    nRecvBytes += nBytes;
    while (nBytes > 0)
    {

        // get current incomplete message, or create a new one
        if (vRecvMsg.empty() ||
                vRecvMsg.back().complete())
            vRecvMsg.push_back(CNetMessage(session->connman.Params().MessageStart(), SER_NETWORK, INIT_PROTO_VERSION));

        CNetMessage& msg = vRecvMsg.back();

        // absorb network data
        int handled;
        if (!msg.in_data)
            handled = msg.readHeader(pch, nBytes);
        else
            handled = msg.readData(pch, nBytes);

        if (handled < 0)
            return false;

        if (msg.in_data && msg.hdr.nMessageSize > MAX_PROTOCOL_MESSAGE_LENGTH)
        {
            LogPrint(BCLog::NET, "Oversized message from peer=%i, disconnecting\n", GetId());
            return false;
        }

        pch += handled;
        nBytes -= handled;

        if (msg.complete())
        {

            //store received bytes per message command
            //to prevent a memory DOS, only allow valid commands
            mapMsgCmdSize::iterator i = mapRecvBytesPerMsgCmd.find(msg.hdr.pchCommand);
            if (i == mapRecvBytesPerMsgCmd.end())
                i = mapRecvBytesPerMsgCmd.find(NET_MESSAGE_COMMAND_OTHER);
            assert(i != mapRecvBytesPerMsgCmd.end());
            i->second += msg.hdr.nMessageSize + CMessageHeader::HEADER_SIZE;

            msg.nTime = nTimeMicros;
            complete = true;
        }
    }

    return true;
}

void CNode::SetSendVersion(int nVersionIn)
{
    // Send version may only be changed in the version message, and
    // only one version message is allowed per session. We can therefore
    // treat this value as const and even atomic as long as it's only used
    // once a version message has been successfully processed. Any attempt to
    // set this twice is an error.
    if (nSendVersion != 0)
        error(logger_, "Send version already set for node: %i. Refusing to change from %i to %i", id, nSendVersion, nVersionIn);
    else
        nSendVersion = nVersionIn;
}

int CNode::GetSendVersion() const
{
    // The send version should always be explicitly set to
    // INIT_PROTO_VERSION rather than using this value until SetSendVersion
    // has been called.
    if (nSendVersion == 0)
    {
        error(logger_, "Requesting unset send version for node: %i. Using %i", id, INIT_PROTO_VERSION);
        return INIT_PROTO_VERSION;
    }
    return nSendVersion;
}


int CNetMessage::readHeader(const char *pch, unsigned int nBytes)
{
    // copy data to temporary parsing buffer
    unsigned int nRemaining = 24 - nHdrPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24)
        return nCopy;

    // deserialize to CMessageHeader
    try
    {
        hdrbuf >> hdr;
    }
    catch (const std::exception&)
    {
        return -1;
    }

    // reject messages larger than MAX_SIZE
    if (hdr.nMessageSize > MAX_SIZE)
        return -1;

    // switch state to reading message data
    in_data = true;

    return nCopy;
}

int CNetMessage::readData(const char *pch, unsigned int nBytes)
{
    unsigned int nRemaining = hdr.nMessageSize - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    if (vRecv.size() < nDataPos + nCopy)
    {
        // Allocate up to 256 KiB ahead, but never more than the total message size.
        vRecv.resize(std::min(hdr.nMessageSize, nDataPos + nCopy + 256 * 1024));
    }

    hasher.Write((const unsigned char*)pch, nCopy);
    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}

const uint256& CNetMessage::GetMessageHash() const
{
    assert(complete());
    if (data_hash.IsNull())
        hasher.Finalize(data_hash.begin());
    return data_hash;
}

bool CConnman::SocketSendFinish(std::shared_ptr<CNode> pnode, int nBytes)
{
    if (!pnode)
        return false;
    LOCK(pnode->cs_vSend);
    auto it = pnode->vSendMsg.begin();
    const auto &data = *it;
    if (nBytes >= 0)
    {
        pnode->nLastSend = GetSystemTimeInSeconds();
        pnode->nSendBytes += nBytes;
        RecordBytesSent(nBytes);
        if (nBytes == data.size())
        {
            pnode->nSendSize -= nBytes;
            pnode->fPauseSend = pnode->nSendSize > nSendBufferMaxSize;
            ++it;
        }
        else
        {
            LogError(BCLog::NET, "async write error, written %ld bytes of %ld\n", nBytes, data.size());
            pnode->CloseSocketDisconnect();
            return false;
        }
    }
    else
    {
        int nErr = WSAGetLastError();
        LogError(BCLog::NET, "socket send error %s\n", NetworkErrorString(nErr));
        pnode->CloseSocketDisconnect();
        return false;
    }
    pnode->vSendMsg.erase(pnode->vSendMsg.begin(), it);
    pnode->sendCompleted = true;
    return true;
}

// requires LOCK(cs_vSend)
void CConnman::SocketSendData(std::shared_ptr<CNode> pnode)
{
    if (!pnode->sendCompleted.exchange(false))
        return;

    auto it = pnode->vSendMsg.begin();

    if (it == pnode->vSendMsg.end())
    {
        pnode->sendCompleted = true;
        return;
    }

    const auto &data = *it;
    {
        pnode->session->async_write(reinterpret_cast<const char*>(data.data()), data.size());
    }
}

struct NodeEvictionCandidate
{
    NodeId      id;
    int64_t     nTimeConnected;
    int64_t     nMinPingUsecTime;
    CAddress    addr;
    uint64_t    nKeyedNetGroup;
};

static bool ReverseCompareNodeMinPingTime(const NodeEvictionCandidate &a, const NodeEvictionCandidate &b)
{
    return a.nMinPingUsecTime > b.nMinPingUsecTime;
}

static bool ReverseCompareNodeTimeConnected(const NodeEvictionCandidate &a, const NodeEvictionCandidate &b)
{
    return a.nTimeConnected > b.nTimeConnected;
}

static bool CompareNetGroupKeyed(const NodeEvictionCandidate &a, const NodeEvictionCandidate &b)
{
    return a.nKeyedNetGroup < b.nKeyedNetGroup;
}

static bool CompareNodeBlockTime(const NodeEvictionCandidate &a, const NodeEvictionCandidate &b)
{
    // There is a fall-through here because it is common for a node to have many peers which have not yet relayed a block.
    return a.nTimeConnected > b.nTimeConnected;
}

static bool CompareNodeTXTime(const NodeEvictionCandidate &a, const NodeEvictionCandidate &b)
{
    // There is a fall-through here because it is common for a node to have more than a few peers that have not yet relayed txn.
    return a.nTimeConnected > b.nTimeConnected;
}


//! Sort an array by the specified comparator, then erase the last K elements.
template<typename T, typename Comparator>
static void EraseLastKElements(std::vector<T> &elements, Comparator comparator, size_t k)
{
    std::sort(elements.begin(), elements.end(), comparator);
    size_t eraseSize = std::min(k, elements.size());
    elements.erase(elements.end() - eraseSize, elements.end());
}

/** Try to find a connection to evict when the node is full.
 *  Extreme care must be taken to avoid opening the node to attacker
 *   triggered network partitioning.
 *  The strategy used here is to protect a small number of peers
 *   for each of several distinct characteristics which are difficult
 *   to forge.  In order to partition a node the attacker must be
 *   simultaneously better at all of them than honest peers.
 */
bool CConnman::AttemptToEvictConnection()
{
    std::vector<NodeEvictionCandidate> vEvictionCandidates;
    {
        LOCK(cs_vNodes);

        for (auto&& node : vNodes)
        {
            if (node->fWhitelisted)
                continue;
            if (!node->fInbound)
                continue;
            if (node->fDisconnect)
                continue;
            NodeEvictionCandidate candidate = {node->GetId(), node->nTimeConnected, node->nMinPingUsecTime,
                                               node->addr, node->nKeyedNetGroup};
            vEvictionCandidates.push_back(candidate);
        }
    }

    // Protect connections with certain characteristics

    // Deterministically select 4 peers to protect by netgroup.
    // An attacker cannot predict which netgroups will be protected
    EraseLastKElements(vEvictionCandidates, CompareNetGroupKeyed, 4);
    // Protect the 8 nodes with the lowest minimum ping time.
    // An attacker cannot manipulate this metric without physically moving nodes closer to the target.
    EraseLastKElements(vEvictionCandidates, ReverseCompareNodeMinPingTime, 8);
    // Protect 4 nodes that most recently sent us transactions.
    // An attacker cannot manipulate this metric without performing useful work.
    EraseLastKElements(vEvictionCandidates, CompareNodeTXTime, 4);
    // Protect 4 nodes that most recently sent us blocks.
    // An attacker cannot manipulate this metric without performing useful work.
    EraseLastKElements(vEvictionCandidates, CompareNodeBlockTime, 4);
    // Protect the half of the remaining nodes which have been connected the longest.
    // This replicates the non-eviction implicit behavior, and precludes attacks that start later.
    EraseLastKElements(vEvictionCandidates, ReverseCompareNodeTimeConnected, vEvictionCandidates.size() / 2);

    if (vEvictionCandidates.empty()) return false;

    // Identify the network group with the most connections and youngest member.
    // (vEvictionCandidates is already sorted by reverse connect time)
    uint64_t naMostConnections;
    unsigned int nMostConnections = 0;
    int64_t nMostConnectionsTime = 0;
    std::map<uint64_t, std::vector<NodeEvictionCandidate> > mapNetGroupNodes;
    for (const NodeEvictionCandidate &node : vEvictionCandidates)
    {
        std::vector<NodeEvictionCandidate> &group = mapNetGroupNodes[node.nKeyedNetGroup];
        group.push_back(node);
        int64_t grouptime = group[0].nTimeConnected;

        if (group.size() > nMostConnections || (group.size() == nMostConnections && grouptime > nMostConnectionsTime))
        {
            nMostConnections = group.size();
            nMostConnectionsTime = grouptime;
            naMostConnections = node.nKeyedNetGroup;
        }
    }

    // Reduce to the network group with the most connections
    vEvictionCandidates = std::move(mapNetGroupNodes[naMostConnections]);

    // Disconnect from the network group with the most connections
    NodeId evicted = vEvictionCandidates.front().id;
    LOCK(cs_vNodes);
    for (auto&& pnode : vNodes)
    {
        if (pnode->GetId() == evicted)
        {
            pnode->fDisconnect = true;
            return true;
        }
    }
    return false;
}

std::shared_ptr<CNode> CConnman::AcceptConnection(std::shared_ptr<AsioSession> session, bool sock_whitelisted)
{
    int nInbound = 0;
    int nMaxInbound = nMaxConnections - (nMaxOutbound + nMaxFeeler);

    boost::system::error_code ec;
    boost::asio::ip::tcp::endpoint endpoint = session->get_socket().remote_endpoint(ec);
    if (ec)
    {
        Log log;
        LOG_FATAL(log) << "CConnman::AcceptConnection - error retrieving remote endpoint with code: " << ec.message();
        trace_and_halt();
    }
    CService saddr = LookupNumeric(endpoint.address().to_string().c_str(), endpoint.port());
    CAddress addr(saddr);

    if (!addr.IsIPv4() && !addr.IsIPv6())
    {
        LogPrintf("Warning: Unknown socket family\n");
        return nullptr;
    }

    bool whitelisted = sock_whitelisted || IsWhitelistedRange(addr);
    {
        LOCK(cs_vNodes);
        for (auto&& pnode : vNodes)
        {
            if (pnode->fInbound) nInbound++;
        }
    }

    if (!fNetworkActive)
    {
        LogPrintf("connection from %s dropped: not accepting new connections\n", addr.ToString());
        return nullptr;
    }

    if (IsBanned(addr) && !whitelisted)
    {
        LogPrint(BCLog::NET, "connection from %s dropped (banned)\n", addr.ToString());
        return nullptr;
    }

    if (nInbound >= nMaxInbound)
    {
        if (!AttemptToEvictConnection())
        {
            // No connection to evict, disconnect the new connection
            LogPrint(BCLog::NET, "failed to find an eviction candidate - connection dropped (full)\n");
            return nullptr;
        }
    }

    NodeId id = GetNewNodeId();
    uint64_t nonce = GetDeterministicRandomizer(RANDOMIZER_ID_LOCALHOSTNONCE).Write(id).Finalize();

    endpoint = session->get_socket().local_endpoint();
    saddr = LookupNumeric(endpoint.address().to_string().c_str(), endpoint.port());
    CAddress addr_bind(saddr);

    std::shared_ptr<CNode> pnode = std::make_shared<CNode>(id, session, addr, CalculateKeyedNetGroup(addr),
                                                           nonce, addr_bind, "", true);
    session->setNode(pnode);
    pnode->fWhitelisted = whitelisted;
    m_msgproc->InitializeNode(pnode);

    LogPrint(BCLog::NET, "connection from %s accepted\n", addr.ToString());

    {
        LOCK(cs_vNodes);
        vNodes.push_back(pnode);
    }

    return pnode;
}

AsioServer::AsioServer(CConnman &conn,
                       boost::asio::ip::address &addr,
                       short port,
                       bool wlisted)
    : p2p(conn.p2p->p2p)
    , connman(conn)
    , acceptor(*conn.io_service, boost::asio::ip::tcp::endpoint(addr, port))
    , whitelisted(wlisted)
    , in_shutdown(false)
    , logger_(conn.logger_)
{
    LogDebug(BCLog::NET, "AsioServer initialized\n");
}

void AsioServer::start()
{
    LogDebug(BCLog::NET, "AsioServer started\n");
    std::shared_ptr<AsioSession> session = std::make_shared<AsioSession>(*connman.io_service, connman);
    acceptor.async_accept(session->get_socket(),
                          boost::bind(&AsioServer::handle_accept, this, shared_from_this(), session,
                                      boost::asio::placeholders::error));
}

AsioServer::~AsioServer()
{
    LogDebug(BCLog::NET, "AsioServer destroyed\n");
}

void AsioServer::handle_accept(std::shared_ptr<AsioServer> ptr,
                               std::shared_ptr<AsioSession> session,
                               const boost::system::error_code& err)
{
    if (err)
    {
        LogError(BCLog::NET, "Error: can't accept connection: %s\n", err.message());
        session.reset();
    }
    else if (!connman.AcceptConnection(session, whitelisted))
    {
        session.reset();
    }
    else
    {
        session->start();
    }
    if (!in_shutdown)
    {
        session = std::make_shared<AsioSession>(*connman.io_service, connman);
        acceptor.async_accept(session->get_socket(),
                              boost::bind(&AsioServer::handle_accept, this, ptr, session,
                                          boost::asio::placeholders::error));
    }
    else
    {
        session = 0;
        LogDebug(BCLog::NET, "AsioServer finished\n");
    }
}

void AsioServer::shutdown()
{
    in_shutdown = true;
    acceptor.cancel();
    LogDebug(BCLog::NET, "AsioServer shutdown\n");
}

bool CConnman::AcceptReceivedBytes(std::shared_ptr<CNode> pnode, const char *pchBuf, int nBytes)
{
    bool res = true;
    if (nBytes > 0)
    {
        bool notify = false;
        if (!pnode->ReceiveMsgBytes(pchBuf, nBytes, notify))
        {
            pnode->CloseSocketDisconnect();
            res = false;
        }
        RecordBytesRecv(nBytes);
        if (notify)
        {
            size_t nSizeAdded = 0;
            auto it(pnode->vRecvMsg.begin());
            for (; it != pnode->vRecvMsg.end(); ++it)
            {
                if (!it->complete())
                    break;
                nSizeAdded += it->vRecv.size() + CMessageHeader::HEADER_SIZE;
            }
            {
                LOCK(pnode->cs_vProcessMsg);
                pnode->vProcessMsg.splice(pnode->vProcessMsg.end(), pnode->vRecvMsg, pnode->vRecvMsg.begin(), it);
                pnode->nProcessQueueSize += nSizeAdded;
                pnode->fPauseRecv = (pnode->nProcessQueueSize > nReceiveFloodSize
                        || pnode->vProcessMsg.size() > nReceiveFloodNMess);
            }
            WakeMessageHandler();
        }
    }
    else if (nBytes == 0)
    {
        // socket closed gracefully
        if (!pnode->fDisconnect)
        {
            LogPrint(BCLog::NET, "socket closed\n");
        }
        pnode->CloseSocketDisconnect();
        res = false;
    }
    else if (nBytes < 0)
    {
        // error
        int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
        {
            if (!pnode->fDisconnect)
                LogPrintf("socket recv error %s\n", NetworkErrorString(nErr));
            pnode->CloseSocketDisconnect();
            res = false;
        }
    }
    return res;
}

void CConnman::ThreadSocketHandler()
{
    unsigned int nPrevNodeCount = 0;
    while (!interruptNet)
    {
        //
        // Wait 1/20 of second or write-to-peer event
        //
        struct timespec tspec;
        clock_gettime(CLOCK_REALTIME, &tspec);
        tspec.tv_nsec += 50000000;
        if (tspec.tv_nsec >= 1000000000)
        {
            tspec.tv_nsec -= 1000000000;
            tspec.tv_sec++;
        }
        sem_timedwait(&dataWritten, &tspec);

        //
        // Disconnect nodes
        //
        {
            LOCK(cs_vNodes);

            if (!fNetworkActive) {
                // Disconnect any connected nodes
                for (auto&& pnode : vNodes)
                {
                    if (!pnode->fDisconnect)
                    {
                        LogPrint(BCLog::NET, "Network not active, dropping peer=%d\n", pnode->GetId());
                        pnode->fDisconnect = true;
                    }
                }
            }

            // Disconnect unused nodes
            std::vector<std::shared_ptr<CNode>> vNodesCopy = vNodes;
            for (auto&& pnode : vNodesCopy)
            {
                if (pnode->fDisconnect)
                {
                    // remove from vNodes
                    vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

                    // release outbound grant (if any)
                    pnode->grantOutbound.Release();

                    // close socket and cleanup
                    pnode->CloseSocketDisconnect();
                }
            }
        }
        size_t vNodesSize;
        {
            LOCK(cs_vNodes);
            vNodesSize = vNodes.size();
        }
        if(vNodesSize != nPrevNodeCount)
        {
            nPrevNodeCount = vNodesSize;
            if(clientInterface)
                clientInterface->NotifyNumConnectionsChanged(vNodesSize);
        }

        //
        // Service each socket
        //
        std::vector<std::shared_ptr<CNode>> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
        }
        for (auto&& pnode : vNodesCopy)
        {
            if (interruptNet)
                return;

            //
            // Send
            //
            if (pnode->sendCompleted)
            {
                LOCK(pnode->cs_vSend);
                SocketSendData(pnode);
            }

            //
            // Inactivity checking
            //
            int64_t nTime = GetSystemTimeInSeconds();
            if (nTime - pnode->nTimeConnected > 60)
            {
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
                {
                    LogPrint(BCLog::NET, "socket no message in first 60 seconds, %d %d from %d\n",
                             pnode->nLastRecv != 0, pnode->nLastSend != 0, pnode->GetId());
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastSend > TIMEOUT_INTERVAL)
                {
                    LogPrintf("socket sending timeout: %is\n", nTime - pnode->nLastSend);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastRecv > TIMEOUT_INTERVAL)
                {
                    LogPrintf("socket receive timeout: %is\n", nTime - pnode->nLastRecv);
                    pnode->fDisconnect = true;
                }
                else if (pnode->nPingNonceSent && pnode->nPingUsecStart + TIMEOUT_INTERVAL * 1000000 < GetTimeMicros())
                {
                    LogPrintf("ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
                    pnode->fDisconnect = true;
                }
                else if (!pnode->fSuccessfullyConnected)
                {
                    LogPrint(BCLog::NET, "version handshake timeout from %d\n", pnode->GetId());
                    pnode->fDisconnect = true;
                }
            }
        }
    }
}

void CConnman::WakeMessageHandler()
{
    {
        std::lock_guard<std::mutex> lock(mutexMsgProc);
        fMsgProcWake = true;
    }
    condMsgProc.notify_one();
}

void CConnman::ThreadDNSAddressSeed()
{
    // goal: only query DNS seeds if address need is acute
    // Avoiding DNS seeds when we don't need them improves user privacy by
    //  creating fewer identifying DNS requests, reduces trust by giving seeds
    //  less influence on the network topology, and reduces traffic to the seeds.
    if ((addrman.size() > 0) && (!Args.GetBoolArg("-forcednsseed", DEFAULT_FORCEDNSSEED)))
    {
        if (!interruptNet.sleep_for(std::chrono::seconds(11)))
            return;

        LOCK(cs_vNodes);
        int nRelevant = 0;
        for (auto&& pnode : vNodes)
        {
            nRelevant += pnode->fSuccessfullyConnected && !pnode->fFeeler && !pnode->fOneShot
                         && !pnode->m_manual_connection && !pnode->fInbound;
        }
        if (nRelevant >= 2)
        {
            LogPrintf("P2P peers available. Skipped DNS seeding.\n");
            return;
        }
    }

    const std::vector<std::string> &vSeeds = Params().DNSSeeds();
    int found = 0;

    LogPrintf("Loading addresses from DNS seeds (could take a while)\n");

    for (const std::string &seed : vSeeds)
    {
        if (interruptNet)
            return;
        // We using DNS Seeds as a oneshot to get nodes.
        AddOneShot(seed);
    }

    LogPrintf("%d addresses found from DNS seeds\n", found);
}

void CConnman::DumpAddresses()
{
    int64_t nStart = GetTimeMillis();

    CAddrDB adb(config, logger_, chainParams);
    adb.Write(addrman);

    LogPrint(BCLog::NET, "Flushed %d addresses to peers.dat  %dms\n",
             addrman.size(), GetTimeMillis() - nStart);
}

void CConnman::DumpData()
{
    LogTrace(BCLog::NET, "Called DumpData()\n");
    DumpAddresses();
    DumpBanlist();
}

void CConnman::ProcessOneShot()
{
    std::string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty())
            return;
        strDest = vOneShots.front();
        vOneShots.pop_front();
    }
    CAddress addr;
    std::shared_ptr<CSemaphoreGrant> grant = std::make_shared<CSemaphoreGrant>(*semOutbound, true);
    if (*grant)
    {
        OpenNetworkConnection(addr, false, grant, strDest.c_str(), true);
    }
}

bool CConnman::GetTryNewOutboundPeer()
{
    return m_try_another_outbound_peer;
}

void CConnman::SetTryNewOutboundPeer(bool flag)
{
    m_try_another_outbound_peer = flag;
    LogPrint(BCLog::NET, "net: setting try another outbound peer=%s\n", flag ? "true" : "false");
}

// Return the number of peers we have over our outbound connection limit
// Exclude peers that are marked for disconnect, or are going to be
// disconnected soon (eg one-shots and feelers)
// Also exclude peers that haven't finished initial connection handshake yet
// (so that we don't decide we're over our desired connection limit, and then
// evict some peer that has finished the handshake)
int CConnman::GetExtraOutboundCount()
{
    int nOutbound = 0;
    {
        LOCK(cs_vNodes);
        for (auto&& pnode : vNodes)
        {
            if (!pnode->fInbound && !pnode->m_manual_connection && !pnode->fFeeler
                    && !pnode->fDisconnect && !pnode->fOneShot && pnode->fSuccessfullyConnected)
                ++nOutbound;
        }
    }
    return std::max(nOutbound - nMaxOutbound, 0);
}

void CConnman::ThreadOpenConnections(const std::vector<std::string> connect)
{
    // Connect to specific addresses
    if (!connect.empty())
    {
        for (int64_t nLoop = 0;; nLoop++)
        {
            ProcessOneShot();
            for (const std::string& strAddr : connect)
            {
                CAddress addr;
                OpenNetworkConnection(addr, false, nullptr, strAddr.c_str(), false, false, true);
                for (int i = 0; i < 10 && i < nLoop; i++)
                {
                    if (!interruptNet.sleep_for(std::chrono::milliseconds(500)))
                        return;
                }
            }
            if (!interruptNet.sleep_for(std::chrono::milliseconds(500)))
                return;
        }
    }

    // Initiate network connections
    int64_t nStart = timeData.GetTime();

    // Minimum time before next feeler connection (in microseconds).
    int64_t nNextFeeler = PoissonNextSend(nStart*1000*1000, FEELER_INTERVAL);
    while (!interruptNet)
    {
        ProcessOneShot();

        if (!interruptNet.sleep_for(std::chrono::milliseconds(5000)))
            return;

        std::shared_ptr<CSemaphoreGrant> grant = std::make_shared<CSemaphoreGrant>(*semOutbound);
        if (interruptNet)
            return;

        // Add seed nodes if DNS seeds are all down (an infrastructure attack?).
        if (addrman.size() == 0 && (timeData.GetTime() - nStart > 60)) {
            static bool done = false;
            if (!done)
            {
                LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
                CNetAddr local;
                local.SetInternal("fixedseeds");
                addrman.Add(convertSeed6(Params().FixedSeeds()), local);
                done = true;
            }
        }

        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Only connect out to one peer per network group (/16 for IPv4).
        int nOutbound = 0;
        std::set<std::vector<unsigned char> > setConnected;
        {
            LOCK(cs_vNodes);
            for (auto&& pnode : vNodes)
            {
                if (!pnode->fInbound && !pnode->m_manual_connection)
                {
                    // Netgroups for inbound and addnode peers are not excluded because our goal here
                    // is to not use multiple of our limited outbound slots on a single netgroup
                    // but inbound and addnode peers do not use our outbound slots.  Inbound peers
                    // also have the added issue that they're attacker controlled and could be used
                    // to prevent us from connecting to particular hosts if we used them here.
                    setConnected.insert(pnode->addr.GetGroup());
                    nOutbound++;
                }
            }
        }

        // Feeler Connections
        //
        // Design goals:
        //  * Increase the number of connectable addresses in the tried table.
        //
        // Method:
        //  * Choose a random address from new and attempt to connect to it if we can connect
        //    successfully it is added to tried.
        //  * Start attempting feeler connections only after node finishes making outbound
        //    connections.
        //  * Only make a feeler connection once every few minutes.
        //
        bool fFeeler = false;

        if (nOutbound >= nMaxOutbound && !GetTryNewOutboundPeer())
        {
            int64_t nTime = GetTimeMicros(); // The current time right now (in microseconds).
            if (nTime > nNextFeeler)
            {
                nNextFeeler = PoissonNextSend(nTime, FEELER_INTERVAL);
                fFeeler = true;
            }
            else
                continue;
        }

        addrman.ResolveCollisions();

        int64_t nANow = timeData.GetAdjustedTime();
        int nTries = 0;
        while (!interruptNet)
        {
            CAddrInfo addr = addrman.SelectTriedCollision();

            // SelectTriedCollision returns an invalid address if it is empty.
            if (!fFeeler || !addr.IsValid())
                addr = addrman.Select(fFeeler);

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr))
            {
                LogTrace(BCLog::NET, "Rejected connection to %s: valid=%d, local=%d, group_count=%d, feeler=%d\n",
                         addr.ToString(), addr.IsValid(), IsLocal(addr), setConnected.count(addr.GetGroup()), fFeeler);
                break;
            }

            // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
            // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new addrman addresses.
            nTries++;
            if (nTries > 100)
                break;

            if (IsLimited(addr))
                continue;

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.nLastTry < 600 && nTries < 30)
                continue;

            // do not allow non-default ports, unless after 50 invalid addresses selected already
            if (addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
                continue;

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid())
        {

            if (fFeeler)
            {
                // Add small amount of random noise before connection to avoid synchronization.
                int randsleep = random_.GetRandInt(FEELER_SLEEP_WINDOW * 1000);
                if (!interruptNet.sleep_for(std::chrono::milliseconds(randsleep)))
                    return;
                LogPrint(BCLog::NET, "Making feeler connection to %s\n", addrConnect.ToString());
            }

            OpenNetworkConnection(addrConnect,
                                  (int)setConnected.size() >= std::min(nMaxConnections - 1, 2),
                                  grant,
                                  nullptr,
                                  false,
                                  fFeeler);
        }
    }
}

std::vector<AddedNodeInfo> CConnman::GetAddedNodeInfo()
{
    std::vector<AddedNodeInfo> ret;

    std::list<std::string> lAddresses(0);
    {
        LOCK(cs_vAddedNodes);
        ret.reserve(vAddedNodes.size());
        std::copy(vAddedNodes.cbegin(), vAddedNodes.cend(), std::back_inserter(lAddresses));
    }


    // Build a map of all already connected addresses (by IP:port and by name) to inbound/outbound and resolved CService
    std::map<CService, bool> mapConnected;
    std::map<std::string, std::pair<bool, CService>> mapConnectedByName;
    {
        LOCK(cs_vNodes);
        for (auto&& pnode : vNodes)
        {
            if (pnode->addr.IsValid())
                mapConnected[pnode->addr] = pnode->fInbound;
            std::string addrName = pnode->GetAddrName();
            if (!addrName.empty())
                mapConnectedByName[std::move(addrName)] = std::make_pair(pnode->fInbound, static_cast<const CService&>(pnode->addr));
        }
    }

    for (const std::string& strAddNode : lAddresses)
    {
        CService service(LookupNumeric(strAddNode.c_str(), Params().GetDefaultPort()));
        AddedNodeInfo addedNode{strAddNode, CService(), false, false};
        if (service.IsValid())
        {
            // strAddNode is an IP:port
            auto it = mapConnected.find(service);
            if (it != mapConnected.end())
            {
                addedNode.resolvedAddress = service;
                addedNode.fConnected = true;
                addedNode.fInbound = it->second;
            }
        }
        else
        {
            // strAddNode is a name
            auto it = mapConnectedByName.find(strAddNode);
            if (it != mapConnectedByName.end())
            {
                addedNode.resolvedAddress = it->second.second;
                addedNode.fConnected = true;
                addedNode.fInbound = it->second.first;
            }
        }
        ret.emplace_back(std::move(addedNode));
    }

    return ret;
}

void CConnman::ThreadOpenAddedConnections()
{
    while (true)
    {
        std::shared_ptr<CSemaphoreGrant> grant = std::make_shared<CSemaphoreGrant>(*semAddnode);
        std::vector<AddedNodeInfo> vInfo = GetAddedNodeInfo();
        bool tried = false;
        for (const AddedNodeInfo& info : vInfo)
        {
            if (!info.fConnected)
            {
                if (!(*grant).TryAcquire())
                {
                    // If we've used up our semaphore and need a new one, let's not wait here since while we are waiting
                    // the addednodeinfo state might change.
                    break;
                }
                tried = true;
                CAddress addr;
                OpenNetworkConnection(addr, false, grant, info.strAddedNode.c_str(), false, false, true);
                if (!interruptNet.sleep_for(std::chrono::milliseconds(500)))
                    return;
            }
        }
        // Retry every 60 seconds if a connection was attempted, otherwise two seconds
        if (!interruptNet.sleep_for(std::chrono::seconds(tried ? 60 : 2)))
            return;
    }
}

// if successful, this moves the passed grant to the constructed node
void CConnman::OpenNetworkConnection(const CAddress& addrConnect,
                                     bool fCountFailure,
                                     std::shared_ptr<CSemaphoreGrant> grantOutbound,
                                     const char *pszDest,
                                     bool fOneShot,
                                     bool fFeeler,
                                     bool manual_connection)
{
    //
    // Initiate outbound network connection
    //
    if (interruptNet)
        return;
    if (!fNetworkActive)
        return;
    if (!pszDest)
    {
        if (IsLocal(addrConnect) ||
                FindNode(static_cast<CNetAddr>(addrConnect)) || IsBanned(addrConnect) ||
                FindNode(addrConnect.ToStringIPPort()))
            return;
    }
    else if (FindNode(std::string(pszDest)))
        return;

    int flags = (fOneShot ? CONN_ONE_SHOT : 0) | (fFeeler ? CONN_FEELER : 0)
            | (manual_connection ? CONN_MANUAL : 0) | (fCountFailure ? CONN_FAILURE : 0);
    ConnectNode(addrConnect, pszDest, grantOutbound, flags);
}

void CConnman::ThreadMessageHandler()
{
    while (!flagInterruptMsgProc)
    {
        std::vector<std::shared_ptr<CNode>> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
        }

        bool fMoreWork = false;

        for (auto&& pnode : vNodesCopy)
        {
            if (pnode->fDisconnect)
                continue;

            // Receive messages
            bool fMoreNodeWork = m_msgproc->ProcessMessages(pnode, flagInterruptMsgProc);
            fMoreWork |= (fMoreNodeWork && !pnode->fPauseSend);
            if (flagInterruptMsgProc)
                return;
            // Send messages
            m_msgproc->SendMessages(pnode);

            if (flagInterruptMsgProc)
                return;
        }

        WAIT_LOCK(mutexMsgProc, lock);
        if (!fMoreWork)
        {
            condMsgProc.wait_until(lock, std::chrono::steady_clock::now() + std::chrono::milliseconds(100), [this]
            {
                return fMsgProcWake;
            });
        }
        fMsgProcWake = false;
    }
}

bool CConnman::BindListenPort(const CService &addrBind, std::string& strError, bool fWhitelisted)
{
    strError = "";
    std::string addr = addrBind.ToStringIP();
    short port = addrBind.GetPort();
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrBind.GetSockAddr((struct sockaddr*)&sockaddr, &len))
    {
        strError = strprintf("Error: Bind address family for %s not supported", addrBind.ToString());
        LogPrintf("%s\n", strError);
        return false;
    }

    ListenSocket sock;

    try
    {
        boost::asio::ip::address asio_addr = boost::asio::ip::make_address(addr);
        sock = std::make_shared<AsioServer>(*this, asio_addr, port, fWhitelisted);
    }
    catch(std::exception &ex)
    {
        strError = strprintf("Error: Unable to bind to %s: %s\n", addrBind.ToString(), ex.what());
        LogPrintf("%s\n", strError);
        return false;
    }

    vhListenSocket.push_back(sock);
    sock->start();
    LogPrintf("Bound to %s\n", addrBind.ToString());

    if (addrBind.IsRoutable() && fDiscover && !fWhitelisted)
        AddLocal(addrBind, LOCAL_BIND);

    return true;
}

void CConnman::Discover()
{
    if (!fDiscover)
        return;

    // Get local host ip
    struct ifaddrs* myaddrs;
    if (getifaddrs(&myaddrs) == 0)
    {
        for (struct ifaddrs* ifa = myaddrs; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == nullptr) continue;
            if ((ifa->ifa_flags & IFF_UP) == 0) continue;
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            if (strcmp(ifa->ifa_name, "lo0") == 0) continue;
            if (ifa->ifa_addr->sa_family == AF_INET)
            {
                struct sockaddr_in* s4 = (struct sockaddr_in*)(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: IPv4 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6)
            {
                struct sockaddr_in6* s6 = (struct sockaddr_in6*)(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: IPv6 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
        }
        freeifaddrs(myaddrs);
    }
}

void CConnman::SetNetworkActive(bool active)
{
    LogPrint(BCLog::NET, "SetNetworkActive: %s\n", active);

    if (fNetworkActive == active)
        return;

    fNetworkActive = active;

    clientInterface->NotifyNetworkActiveChanged(fNetworkActive);
}

CConnman::CConnman(uint64_t nSeed0In,
                   uint64_t nSeed1In,
                   p2p_config &conf,
                   ArgsManager &ArgsIn,
                   TimeData &timeDataIn,
                   Random &random)
    : nSeed0(nSeed0In)
    , nSeed1(nSeed1In)
    , config(conf)
    , Args(ArgsIn)
    , timeData(timeDataIn)
    , addrman(timeData, random)
    , logger_(timeData.logger_)
    , random_(random)
    , vfLimited()
{
    fNetworkActive = true;
    setBannedIsDirty = false;
    fAddressesInitialized = false;
    nLastNodeId = 0;
    nSendBufferMaxSize = 0;
    nReceiveFloodSize = 0;
    nReceiveFloodNMess = 0;
    flagInterruptMsgProc = false;
    SetTryNewOutboundPeer(false);
    fDiscover = true;
    fListen = true;

    Options connOptions;
    Init(connOptions);
    sem_init(&dataWritten, 0, 0);
}

NodeId CConnman::GetNewNodeId()
{
    return nLastNodeId.fetch_add(1, std::memory_order_relaxed);
}


bool CConnman::Bind(const CService &addr, unsigned int flags)
{
    if (!(flags & BF_EXPLICIT) && IsLimited(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0))
    {
        if ((flags & BF_REPORT_ERROR) && clientInterface)
            clientInterface->InitError(strError);
        return false;
    }
    return true;
}

bool CConnman::InitBinds(const std::vector<CService>& binds, const std::vector<CService>& whiteBinds)
{
    bool fBound = false;
    for (const auto& addrBind : binds)
    {
        fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
    }
    for (const auto& addrBind : whiteBinds)
    {
        fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
    }
    if (binds.empty() && whiteBinds.empty())
    {
        struct in_addr inaddr_any;
        inaddr_any.s_addr = INADDR_ANY;
        struct in6_addr inaddr6_any = IN6ADDR_ANY_INIT;
        fBound |= Bind(CService(inaddr6_any, GetListenPort()), BF_NONE);
        fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
    }
    return fBound;
}

bool CConnman::LoadData()
{
    bool res = true;

    if (clientInterface)
    {
        clientInterface->InitMessage(_("Loading P2P addresses..."));
    }
    // Load addresses from peers.dat
    int64_t nStart = GetTimeMillis();
    {
        CAddrDB adb(config, logger_, chainParams);
        if (adb.Read(addrman))
            LogPrintf("Loaded %i addresses from peers.dat  %dms\n", addrman.size(), GetTimeMillis() - nStart);
        else
        {
            addrman.Clear(); // Addrman can be in an inconsistent state after failure, reset it
            LogPrintf("Invalid or missing peers.dat; recreating\n");
            DumpAddresses();
            res = false;
        }
    }
    if (clientInterface)
        clientInterface->InitMessage(_("Loading banlist..."));
    // Load addresses from banlist.dat
    nStart = GetTimeMillis();
    CBanDB bandb(config, logger_, chainParams);
    banmap_t banmap;
    if (bandb.Read(banmap))
    {
        SetBanned(banmap); // thread save setter
        SetBannedSetDirty(false); // no need to write down, just read data
        SweepBanned(); // sweep out unused entries

        LogPrint(BCLog::NET, "Loaded %d banned node ips/subnets from banlist.dat  %dms\n",
                 banmap.size(), GetTimeMillis() - nStart);
    }
    else
    {
        LogPrintf("Invalid or missing banlist.dat; recreating\n");
        SetBannedSetDirty(true); // force write
        DumpBanlist();
        res = false;
    }

    return res;
}

bool CConnman::Start(const Options& connOptions)
{
    Init(connOptions);

    {
        LOCK(cs_totalBytesRecv);
        nTotalBytesRecv = 0;
    }
    {
        LOCK(cs_totalBytesSent);
        nTotalBytesSent = 0;
        nMaxOutboundTotalBytesSentInCycle = 0;
        nMaxOutboundCycleStartTime = 0;
    }

    if (fListen && !InitBinds(connOptions.vBinds, connOptions.vWhiteBinds))
    {
        if (clientInterface)
            clientInterface->InitError(_("Failed to listen on any port. Use -listen=0 if you want this."));
        return false;
    }

    for (const auto& strDest : connOptions.vSeedNodes)
    {
        AddOneShot(strDest);
    }

    LoadData();

    clientInterface->InitMessage(_("Starting network threads..."));

    fAddressesInitialized = true;

    if (semOutbound == nullptr)
    {
        // initialize semaphore
        semOutbound = MakeUnique<CSemaphore>(std::min((nMaxOutbound + nMaxFeeler), nMaxConnections));
    }
    if (semAddnode == nullptr)
    {
        // initialize semaphore
        semAddnode = MakeUnique<CSemaphore>(nMaxAddnode);
    }

    //
    // Start threads
    //
    assert(m_msgproc);
    interruptNet.reset();
    flagInterruptMsgProc = false;

    {
        LOCK(mutexMsgProc);
        fMsgProcWake = false;
    }

    // Send and receive from sockets, accept connections
    threadSocketHandler = std::thread(&TraceThread<std::function<void()> >,
                                      "net",
                                      &logger_,
                                      std::function<void()>(std::bind(&CConnman::ThreadSocketHandler, this)));

    if (!Args.GetBoolArg("-dnsseed", true))
        LogPrintf("DNS seeding disabled\n");
    else
        threadDNSAddressSeed = std::thread(&TraceThread<std::function<void()> >,
                                           "dnsseed",
                                           &logger_,
                                           std::function<void()>(std::bind(&CConnman::ThreadDNSAddressSeed, this)));

    // Initiate outbound connections from -addnode
    threadOpenAddedConnections = std::thread(&TraceThread<std::function<void()> >,
                                             "addcon",
                                             &logger_,
                                             std::function<void()>(std::bind(&CConnman::ThreadOpenAddedConnections, this)));

    if (connOptions.m_use_addrman_outgoing && !connOptions.m_specified_outgoing.empty())
    {
        if (clientInterface)
            clientInterface->InitError(_("Cannot provide specific connections and have addrman find outgoing connections at the same."));
        return false;
    }
    if (connOptions.m_use_addrman_outgoing || !connOptions.m_specified_outgoing.empty())
        threadOpenConnections = std::thread(&TraceThread<std::function<void()> >,
                                            "opencon",
                                            &logger_,
                                            std::function<void()>(std::bind(&CConnman::ThreadOpenConnections,
                                                                            this,
                                                                            connOptions.m_specified_outgoing)));

    // Process messages
    threadMessageHandler = std::thread(&TraceThread<std::function<void()> >,
                                       "msghand",
                                       &logger_,
                                       std::function<void()>(std::bind(&CConnman::ThreadMessageHandler, this)));

    // Dump network addresses
    scheduleEvery(std::bind(&CConnman::DumpData, this), DUMP_ADDRESSES_INTERVAL * 1000);

    return true;
}

void CConnman::Interrupt()
{
    {
        std::lock_guard<std::mutex> lock(mutexMsgProc);
        flagInterruptMsgProc = true;
    }
    condMsgProc.notify_all();

    interruptNet();

    if (semOutbound)
    {
        for (int i=0; i<(nMaxOutbound + nMaxFeeler); i++)
        {
            semOutbound->post();
        }
    }

    if (semAddnode)
    {
        for (int i=0; i<nMaxAddnode; i++)
        {
            semAddnode->post();
        }
    }
}

void CConnman::Stop()
{
    if (threadMessageHandler.joinable())
        threadMessageHandler.join();
    if (threadOpenConnections.joinable())
        threadOpenConnections.join();
    if (threadOpenAddedConnections.joinable())
        threadOpenAddedConnections.join();
    if (threadDNSAddressSeed.joinable())
        threadDNSAddressSeed.join();
    if (threadSocketHandler.joinable())
        threadSocketHandler.join();

    if (fAddressesInitialized)
    {
        DumpData();
        fAddressesInitialized = false;
    }

    // Close sockets
    for (auto&& pnode : vNodes)
        pnode->CloseSocketDisconnect();
    for (ListenSocket& hListenSocket : vhListenSocket)
    {
        hListenSocket->shutdown();
        hListenSocket = 0;
    }

    vNodes.clear();
    vhListenSocket.clear();
    semOutbound.reset();
    semAddnode.reset();
}

CConnman::~CConnman()
{
    Interrupt();
    Stop();
    sem_destroy(&dataWritten);
}

void CConnman::MarkAddressGood(const CAddress& addr)
{
    addrman.Good(addr);
}

void CConnman::AddNewAddresses(const std::vector<CAddress>& vAddr, const CAddress& addrFrom, int64_t nTimePenalty)
{
    addrman.Add(vAddr, addrFrom, nTimePenalty);
}

std::vector<CAddress> CConnman::GetAddresses()
{
    return addrman.GetAddr();
}

bool CConnman::AddNode(const std::string& strNode)
{
    LOCK(cs_vAddedNodes);
    for (const std::string& it : vAddedNodes)
    {
        if (strNode == it) return false;
    }

    vAddedNodes.push_back(strNode);
    return true;
}

bool CConnman::RemoveAddedNode(const std::string& strNode)
{
    LOCK(cs_vAddedNodes);
    for(std::vector<std::string>::iterator it = vAddedNodes.begin(); it != vAddedNodes.end(); ++it)
    {
        if (strNode == *it)
        {
            vAddedNodes.erase(it);
            return true;
        }
    }
    return false;
}

bool CConnman::DisconnectNode(const std::string& strNode)
{
    LOCK(cs_vNodes);
    if (auto&& pnode = FindNode(strNode))
    {
        pnode->fDisconnect = true;
        return true;
    }
    return false;
}
bool CConnman::DisconnectNode(NodeId id)
{
    LOCK(cs_vNodes);
    for(auto&& pnode : vNodes)
    {
        if (id == pnode->GetId())
        {
            pnode->fDisconnect = true;
            return true;
        }
    }
    return false;
}

void CConnman::RecordBytesRecv(uint64_t bytes)
{
    LOCK(cs_totalBytesRecv);
    nTotalBytesRecv += bytes;
}

void CConnman::RecordBytesSent(uint64_t bytes)
{
    LOCK(cs_totalBytesSent);
    nTotalBytesSent += bytes;

    uint64_t now = timeData.GetTime();
    if (nMaxOutboundCycleStartTime + nMaxOutboundTimeframe < now)
    {
        // timeframe expired, reset cycle
        nMaxOutboundCycleStartTime = now;
        nMaxOutboundTotalBytesSentInCycle = 0;
    }

    // TODO, exclude whitebind peers
    nMaxOutboundTotalBytesSentInCycle += bytes;
}

void CConnman::SetMaxOutboundTarget(uint64_t limit)
{
    LOCK(cs_totalBytesSent);
    nMaxOutboundLimit = limit;
}

uint64_t CConnman::GetMaxOutboundTarget()
{
    LOCK(cs_totalBytesSent);
    return nMaxOutboundLimit;
}

uint64_t CConnman::GetMaxOutboundTimeframe()
{
    LOCK(cs_totalBytesSent);
    return nMaxOutboundTimeframe;
}

uint64_t CConnman::GetMaxOutboundTimeLeftInCycle()
{
    LOCK(cs_totalBytesSent);
    if (nMaxOutboundLimit == 0)
        return 0;

    if (nMaxOutboundCycleStartTime == 0)
        return nMaxOutboundTimeframe;

    uint64_t cycleEndTime = nMaxOutboundCycleStartTime + nMaxOutboundTimeframe;
    uint64_t now = timeData.GetTime();
    return (cycleEndTime < now) ? 0 : cycleEndTime - timeData.GetTime();
}

void CConnman::SetMaxOutboundTimeframe(uint64_t timeframe)
{
    LOCK(cs_totalBytesSent);
    if (nMaxOutboundTimeframe != timeframe)
    {
        // reset measure-cycle in case of changing
        // the timeframe
        nMaxOutboundCycleStartTime = timeData.GetTime();
    }
    nMaxOutboundTimeframe = timeframe;
}

bool CConnman::OutboundTargetReached(bool historicalBlockServingLimit)
{
    LOCK(cs_totalBytesSent);
    if (nMaxOutboundLimit == 0)
        return false;

    if (historicalBlockServingLimit)
    {
        // keep a large enough buffer to at least relay each block once
        uint64_t timeLeftInCycle = GetMaxOutboundTimeLeftInCycle();
        uint64_t buffer = timeLeftInCycle / 600 * MAX_BLOCK_SERIALIZED_SIZE;
        if (buffer >= nMaxOutboundLimit || nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit - buffer)
            return true;
    }
    else if (nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit)
        return true;

    return false;
}

uint64_t CConnman::GetOutboundTargetBytesLeft()
{
    LOCK(cs_totalBytesSent);
    if (nMaxOutboundLimit == 0)
        return 0;

    return (nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit) ? 0 : nMaxOutboundLimit - nMaxOutboundTotalBytesSentInCycle;
}

uint64_t CConnman::GetTotalBytesRecv()
{
    LOCK(cs_totalBytesRecv);
    return nTotalBytesRecv;
}

uint64_t CConnman::GetTotalBytesSent()
{
    LOCK(cs_totalBytesSent);
    return nTotalBytesSent;
}

unsigned int CConnman::GetReceiveFloodSize() const
{
    return nReceiveFloodSize;
}

unsigned int CConnman::GetReceiveFloodNMess() const
{
    return nReceiveFloodNMess;
}

CNode::CNode(NodeId idIn,
             std::shared_ptr<AsioSession> sessionIn,
             const CAddress& addrIn,
             uint64_t nKeyedNetGroupIn,
             uint64_t nLocalHostNonceIn,
             const CAddress &addrBindIn,
             const std::string& addrNameIn,
             bool fInboundIn)
    : nTimeConnected(GetSystemTimeInSeconds())
    , addr(addrIn)
    , addrBind(addrBindIn)
    , fInbound(fInboundIn)
    , nKeyedNetGroup(nKeyedNetGroupIn)
    , addrKnown(sessionIn->connman.random_, 5000, 0.001)
    , id(idIn)
    , nLocalHostNonce(nLocalHostNonceIn)
    , nSendVersion(0)
    , logger_(sessionIn->connman.logger_)
    , connman(sessionIn->connman)
{
    session = sessionIn;
    nRecvVersion = INIT_PROTO_VERSION;
    nLastSend = 0;
    nLastRecv = 0;
    nSendBytes = 0;
    nRecvBytes = 0;
    nTimeOffset = 0;
    addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
    nVersion = 0;
    strSubVer = "";
    fWhitelisted = false;
    fOneShot = false;
    m_manual_connection = false;
    fFeeler = false;
    fSuccessfullyConnected = false;
    fDisconnect = false;
    nSendSize = 0;
    hashContinue = uint256();
    fGetAddr = false;
    nNextLocalAddrSend = 0;
    nNextAddrSend = 0;
    fSentAddr = false;
    nPingNonceSent = 0;
    nPingUsecStart = 0;
    nPingUsecTime = 0;
    fPingQueued = false;
    nMinPingUsecTime = std::numeric_limits<int64_t>::max();
    fPauseRecv = false;
    fPauseSend = false;
    nProcessQueueSize = 0;
    first_propagate_index = session->connman.p2p_store->GetNextLabel();
    next_propagate_index = 0;
    sendCompleted = true;

    for (const std::string &msg : getAllNetMessageTypes())
        mapRecvBytesPerMsgCmd[msg] = 0;
    mapRecvBytesPerMsgCmd[NET_MESSAGE_COMMAND_OTHER] = 0;

    if (session->connman.fLogIPs)
        LogPrint(BCLog::NET, "Added connection to %s peer=%d\n", addrName, id);
    else
        LogPrint(BCLog::NET, "Added connection peer=%d\n", id);
}

CNode::~CNode()
{
    session->shutdown();
    LOCK(cs_vSend);
    connman.FinalizeNode(id, addr);
    LogDebug(BCLog::NET, "Node destroyed, peer=%d\n", id);
}

void CConnman::FinalizeNode(NodeId id, const CAddress &addr)
{
    bool fUpdateConnectionTime = false;
    m_msgproc->FinalizeNode(id, fUpdateConnectionTime);
    if (fUpdateConnectionTime)
        addrman.Connected(addr);
}

bool CConnman::NodeFullyConnected(std::shared_ptr<CNode> pnode)
{
    return pnode && pnode->fSuccessfullyConnected && !pnode->fDisconnect;
}

void CConnman::PushMessage(std::shared_ptr<CNode> pnode, CSerializedNetMsg&& msg)
{
    size_t nMessageSize = msg.data.size();
    size_t nTotalSize = nMessageSize + CMessageHeader::HEADER_SIZE;
    LogTrace(BCLog::NET, "sending %s (%d bytes) peer=%d\n",  SanitizeString(msg.command.c_str()), nMessageSize, pnode->GetId());

    std::vector<unsigned char> serializedHeader;
    serializedHeader.reserve(CMessageHeader::HEADER_SIZE);
    uint256 hash = Hash(msg.data.data(), msg.data.data() + nMessageSize);
    CMessageHeader hdr(Params().MessageStart(), msg.command.c_str(), nMessageSize);
    memcpy(hdr.pchChecksum, hash.begin(), CMessageHeader::CHECKSUM_SIZE);

    CVectorWriter{SER_NETWORK, INIT_PROTO_VERSION, serializedHeader, 0, hdr};

    {
        LOCK(pnode->cs_vSend);
        bool optimisticSend(pnode->vSendMsg.empty());

        //log total amount of bytes per command
        pnode->mapSendBytesPerMsgCmd[msg.command] += nTotalSize;
        pnode->nSendSize += nTotalSize;

        if (pnode->nSendSize > nSendBufferMaxSize)
            pnode->fPauseSend = true;
        pnode->vSendMsg.push_back(std::move(serializedHeader));
        if (nMessageSize)
            pnode->vSendMsg.push_back(std::move(msg.data));

        // If write queue empty, attempt "optimistic write"
        if (optimisticSend == true && pnode->sendCompleted)
            SocketSendData(pnode);
        else
            // else push sending thread
            sem_post(&dataWritten);
    }
}

bool CConnman::ForNode(NodeId id, std::function<bool(std::shared_ptr<CNode> pnode)> func)
{
    std::shared_ptr<CNode> found;
    LOCK(cs_vNodes);
    for (auto&& pnode : vNodes)
    {
        if(pnode->GetId() == id)
        {
            found = pnode;
            break;
        }
    }
    return found && NodeFullyConnected(found) && func(found);
}

int64_t CConnman::PoissonNextSend(int64_t now, int average_interval_seconds)
{
    return now + (int64_t)(log1p(random_.GetRand(1ULL << 48)
            * -0.0000000000000035527136788 /* -1/2^48 */)
            * average_interval_seconds * -1000000.0 + 0.5);
}

CSipHasher CConnman::GetDeterministicRandomizer(uint64_t id) const
{
    return CSipHasher(nSeed0, nSeed1).Write(id);
}

uint64_t CConnman::CalculateKeyedNetGroup(const CAddress& ad) const
{
    std::vector<unsigned char> vchNetGroup(ad.GetGroup());

    return GetDeterministicRandomizer(RANDOMIZER_ID_NETGROUP).Write(vchNetGroup.data(), vchNetGroup.size()).Finalize();
}
