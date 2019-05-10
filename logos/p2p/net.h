// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include <atomic>
#include <deque>
#include <stdint.h>
#include <thread>
#include <memory>
#include <condition_variable>
#include <semaphore.h>
#include <arpa/inet.h>
#include <boost/asio.hpp>
#include <addrdb.h>
#include <addrman.h>
#include <bloom.h>
#include <compat.h>
#include <hash.h>
#include <netaddress.h>
#include <p2p.h>
#include <propagate.h>
#include <protocol.h>
#include <random.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>
#include <threadinterrupt.h>
#include <chainparams.h>

class CNode;

/** Time between pings automatically sent out for latency probing and keepalive (in seconds). */
constexpr int PING_INTERVAL = 2 * 60;
/** Time after which to disconnect, after waiting for a ping response (or inactivity). */
constexpr int TIMEOUT_INTERVAL = 20 * 60;
/** Run the feeler connection loop once every 2 minutes or 120 seconds. **/
constexpr int FEELER_INTERVAL = 120;
/** The maximum number of new addresses to accumulate before announcing. */
constexpr unsigned int MAX_ADDR_TO_SEND = 1000;
/** Maximum length of incoming protocol messages (no message over 4 MB is currently acceptable). */
constexpr unsigned int MAX_PROTOCOL_MESSAGE_LENGTH = 4 * 1000 * 1000;
/** Maximum length of strSubVer in `version` message */
constexpr unsigned int MAX_SUBVERSION_LENGTH = 256;
/** Maximum number of automatic outgoing nodes */
constexpr int MAX_OUTBOUND_CONNECTIONS = 8;
/** Maximum number of addnode outgoing nodes */
constexpr int MAX_ADDNODE_CONNECTIONS = 8;
/** -listen default */
constexpr bool DEFAULT_LISTEN = true;
/** The maximum number of peer connections to maintain. */
constexpr unsigned int DEFAULT_MAX_PEER_CONNECTIONS = 125;
/** The default for -maxuploadtarget. 0 = Unlimited */
constexpr uint64_t DEFAULT_MAX_UPLOAD_TARGET = 0;
/** The default timeframe for -maxuploadtarget. 1 day. */
constexpr uint64_t MAX_UPLOAD_TIMEFRAME = 60 * 60 * 24;

constexpr bool DEFAULT_FORCEDNSSEED = false;
constexpr size_t DEFAULT_MAXRECEIVEBUFFER = 5 * 1000;
constexpr size_t DEFAULT_MAXSENDBUFFER    = 1 * 1000;

// NOTE: When adjusting this, update rpcnet:setban's help ("24h")
constexpr unsigned int DEFAULT_MISBEHAVING_BANTIME = 60 * 60 * 24;  // Default 24-hour ban

typedef int64_t NodeId;

struct AddedNodeInfo
{
    std::string     strAddedNode;
    CService        resolvedAddress;
    bool            fConnected;
    bool            fInbound;
};

class CClientUIInterface;

struct CSerializedNetMsg
{
    CSerializedNetMsg() = default;
    CSerializedNetMsg(CSerializedNetMsg&&) = default;
    CSerializedNetMsg& operator=(CSerializedNetMsg&&) = default;
    // No copying, only moves.
    CSerializedNetMsg(const CSerializedNetMsg& msg) = delete;
    CSerializedNetMsg& operator=(const CSerializedNetMsg&) = delete;

    std::vector<unsigned char>  data;
    std::string                 command;
};

class CConnman;

class AsioSession : public std::enable_shared_from_this<AsioSession>
{
public:
    AsioSession(boost::asio::io_service& ios,
                CConnman &connman_);
    ~AsioSession();
    boost::asio::ip::tcp::socket& get_socket()
    {
        return socket;
    }
    void setNode(std::shared_ptr<CNode> pnode_);
    void start();
    void shutdown();
    void async_write(const char *buf, size_t bytes);
    CConnman &                      connman;
private:
    BCLog::Logger &                 logger_;
    boost::asio::ip::tcp::socket    socket;
    std::shared_ptr<CNode>          pnode;
    NodeId                          id;
    bool                            in_shutdown;
    void handle_read(std::shared_ptr<AsioSession> s, const boost::system::error_code& err,
                     size_t bytes_transferred);
    void handle_write(std::shared_ptr<AsioSession> s, const boost::system::error_code& err,
                      size_t bytes_transferred);
    enum
    {
        max_length = 0x10000
    };
    char                            data[max_length];
};

enum ConnFlags
{
    CONN_ONE_SHOT	= 1,
    CONN_FEELER     = 2,
    CONN_MANUAL     = 4,
    CONN_FAILURE	= 8,
};

class AsioClient
{
public:
    AsioClient(CConnman &conn,
               const char *nam,
               std::shared_ptr<CSemaphoreGrant> grant,
               int fl);
    ~AsioClient();
    void connect(const std::string &host, const std::string &port);
    void resolve_handler(const boost::system::error_code& ec,
                         boost::asio::ip::tcp::resolver::results_type results);
    void connect_handler(std::shared_ptr<AsioSession> session, const boost::system::error_code& ec,
                         const boost::asio::ip::tcp::endpoint& endpoint);
private:
    CConnman &                          connman;
    BCLog::Logger &                     logger_;
    char *                              name;
    std::shared_ptr<CSemaphoreGrant>    grantOutbound;
    int                                 flags;
    boost::asio::ip::tcp::resolver      resolver;
    friend class CConnman;
};

class AsioServer : public std::enable_shared_from_this<AsioServer>
{
public:
    AsioServer(CConnman &,
               boost::asio::ip::address &,
               short port,
               bool wlisted);
    ~AsioServer();
    void start();
    void shutdown();
private:
    CConnman &                      connman;
    BCLog::Logger &                 logger_;
    boost::asio::ip::tcp::acceptor  acceptor;
    bool                            whitelisted;
    bool                            in_shutdown;
    void handle_accept(std::shared_ptr<AsioServer>, std::shared_ptr<AsioSession>, const boost::system::error_code&);
    friend class CConnman;
};

enum
{
    LOCAL_NONE,   // unknown
    LOCAL_IF,     // address a local interface listens on
    LOCAL_BIND,   // address explicit bound to
    LOCAL_MANUAL, // address explicitly specified (-externalip=)

    LOCAL_MAX
};

struct LocalServiceInfo
{
    int nScore;
    int nPort;
};

class NetEventsInterface;
class CConnman
{
public:

    enum NumConnections
    {
        CONNECTIONS_NONE    = 0,
        CONNECTIONS_IN      = (1U << 0),
        CONNECTIONS_OUT     = (1U << 1),
        CONNECTIONS_ALL     = (CONNECTIONS_IN | CONNECTIONS_OUT),
    };

    struct Options
    {
        int                         nMaxConnections = 0;
        int                         nMaxOutbound = 0;
        int                         nMaxAddnode = 0;
        int                         nMaxFeeler = 0;
        CClientUIInterface*         uiInterface = nullptr;
        NetEventsInterface*         m_msgproc = nullptr;
        unsigned int                nSendBufferMaxSize = 0;
        unsigned int                nReceiveFloodSize = 0;
        uint64_t                    nMaxOutboundTimeframe = 0;
        uint64_t                    nMaxOutboundLimit = 0;
        std::vector<std::string>    vSeedNodes;
        std::vector<CSubNet>        vWhitelistedRange;
        std::vector<CService>       vBinds,
                                    vWhiteBinds;
        bool                        m_use_addrman_outgoing = true;
        std::vector<std::string>    m_specified_outgoing;
        std::vector<std::string>    m_added_nodes;
    };

    void Init(const Options& connOptions)
    {
        nMaxConnections = connOptions.nMaxConnections;
        nMaxOutbound = std::min(connOptions.nMaxOutbound, connOptions.nMaxConnections);
        nMaxAddnode = connOptions.nMaxAddnode;
        nMaxFeeler = connOptions.nMaxFeeler;
        clientInterface = connOptions.uiInterface;
        m_msgproc = connOptions.m_msgproc;
        nSendBufferMaxSize = connOptions.nSendBufferMaxSize;
        nReceiveFloodSize = connOptions.nReceiveFloodSize;
        {
            LOCK(cs_totalBytesSent);
            nMaxOutboundTimeframe = connOptions.nMaxOutboundTimeframe;
            nMaxOutboundLimit = connOptions.nMaxOutboundLimit;
        }
        vWhitelistedRange = connOptions.vWhitelistedRange;
        {
            LOCK(cs_vAddedNodes);
            vAddedNodes = connOptions.m_added_nodes;
        }
    }

    CConnman(uint64_t seed0,
             uint64_t seed1,
             p2p_config &config,
             ArgsManager &Args,
             TimeData &timeDataIn,
             Random &random);
    ~CConnman();
    bool Start(const Options& options);
    void Stop();
    void Interrupt();
    bool GetNetworkActive() const
    {
        return fNetworkActive;
    };
    void SetNetworkActive(bool active);
    void OpenNetworkConnection(const CAddress& addrConnect,
                               bool fCountFailure,
                               std::shared_ptr<CSemaphoreGrant> grantOutbound,
                               const char *strDest = nullptr,
                               bool fOneShot = false,
                               bool fFeeler = false,
                               bool manual_connection = false);
    bool CheckIncomingNonce(uint64_t nonce);

    bool ForNode(NodeId id, std::function<bool(std::shared_ptr<CNode> pnode)> func);

    void PushMessage(std::shared_ptr<CNode> pnode, CSerializedNetMsg&& msg);

    template<typename Callable>
    void ForEachNode(Callable&& func)
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes)
        {
            if (NodeFullyConnected(node))
                func(node);
        }
    };

    template<typename Callable>
    void ForEachNode(Callable&& func) const
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes)
        {
            if (NodeFullyConnected(node))
                func(node);
        }
    };

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable&& pre, CallableAfter&& post)
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes)
        {
            if (NodeFullyConnected(node))
                pre(node);
        }
        post();
    };

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable&& pre, CallableAfter&& post) const
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes)
        {
            if (NodeFullyConnected(node))
                pre(node);
        }
        post();
    };

    // Addrman functions
    void MarkAddressGood(const CAddress& addr);
    void AddNewAddresses(const std::vector<CAddress>& vAddr, const CAddress& addrFrom, int64_t nTimePenalty = 0);
    std::vector<CAddress> GetAddresses();

    // Denial-of-service detection/prevention
    // The idea is to detect peers that are behaving
    // badly and disconnect/ban them, but do it in a
    // one-coding-mistake-won't-shatter-the-entire-network
    // way.
    // IMPORTANT:  There should be nothing I can give a
    // node that it will forward on that will make that
    // node's peers drop it. If there is, an attacker
    // can isolate a node and/or try to split the network.
    // Dropping a node for sending stuff that is invalid
    // now but might be valid in a later version is also
    // dangerous, because it can cause a network split
    // between nodes running old code and nodes running
    // new code.
    void Ban(const CNetAddr& netAddr, const BanReason& reason, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    void Ban(const CSubNet& subNet, const BanReason& reason, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    void ClearBanned(); // needed for unit testing
    bool IsBanned(CNetAddr ip);
    bool IsBanned(CSubNet subnet);
    bool Unban(const CNetAddr &ip);
    bool Unban(const CSubNet &ip);
    void GetBanned(banmap_t &banmap);
    void SetBanned(const banmap_t &banmap);
    bool LoadData();
    void DumpData();

    // This allows temporarily exceeding nMaxOutbound, with the goal of finding
    // a peer that is better than all our current peers.
    void SetTryNewOutboundPeer(bool flag);
    bool GetTryNewOutboundPeer();

    // Return the number of outbound peers we have in excess of our target (eg,
    // if we previously called SetTryNewOutboundPeer(true), and have since set
    // to false, we may have extra peers that we wish to disconnect). This may
    // return a value less than (num_outbound_connections - num_outbound_slots)
    // in cases where some outbound connections are not yet fully connected, or
    // not yet fully disconnected.
    int GetExtraOutboundCount();

    void Discover();
    bool AddLocal(const CNetAddr& addr, int nScore = LOCAL_NONE);
    bool AddLocal(const CService& addr, int nScore = LOCAL_NONE);
    bool AddNode(const std::string& node);
    bool RemoveAddedNode(const std::string& node);
    std::vector<AddedNodeInfo> GetAddedNodeInfo();

    bool DisconnectNode(const std::string& node);
    bool DisconnectNode(NodeId id);
    void FinalizeNode(NodeId id, const CAddress &addr);

    CAddress GetLocalAddress(const CNetAddr *paddrPeer);
    unsigned short GetListenPort();
    bool IsPeerAddrLocalGood(std::shared_ptr<CNode> pnode);
    void AdvertiseLocal(std::shared_ptr<CNode> pnode);
    bool SeenLocal(const CService& addr);
    bool IsReachable(const CNetAddr &addr);
    void SetLimited(enum Network net, bool fLimited = true);

    //!set the max outbound target in bytes
    void SetMaxOutboundTarget(uint64_t limit);
    uint64_t GetMaxOutboundTarget();

    //!set the timeframe for the max outbound target
    void SetMaxOutboundTimeframe(uint64_t timeframe);
    uint64_t GetMaxOutboundTimeframe();

    //!check if the outbound target is reached
    // if param historicalBlockServingLimit is set true, the function will
    // response true if the limit for serving historical blocks has been reached
    bool OutboundTargetReached(bool historicalBlockServingLimit);

    //!response the bytes left in the current max outbound cycle
    // in case of no limit, it will always response 0
    uint64_t GetOutboundTargetBytesLeft();

    //!response the time in second left in the current max outbound cycle
    // in case of no limit, it will always response 0
    uint64_t GetMaxOutboundTimeLeftInCycle();

    uint64_t GetTotalBytesRecv();
    uint64_t GetTotalBytesSent();

    /** Get a unique deterministic randomizer. */
    CSipHasher GetDeterministicRandomizer(uint64_t id) const;

    unsigned int GetReceiveFloodSize() const;

    void WakeMessageHandler();

    void scheduleEveryRecurse(std::function<void()> const &handler, unsigned ms)
    {
        handler();
        scheduleAfter(std::bind(&CConnman::scheduleEveryRecurse, this, handler, ms), ms);
    }

    void scheduleEvery(std::function<void()> const &handler, unsigned ms)
    {
        scheduleAfter(std::bind(&CConnman::scheduleEveryRecurse, this, handler, ms), ms);
    }

    /** Return a timestamp in the future (in microseconds) for exponentially distributed events. */
    int64_t PoissonNextSend(int64_t now, int average_interval_seconds);

    p2p_config &                                                    config;
    ArgsManager &                                                   Args;
    TimeData &                                                      timeData;
    BCLog::Logger &                                                 logger_;
    Random &                                                        random_;
    p2p_interface *                                                 p2p;
    CClientUIInterface*                                             clientInterface;
    PropagateStore *                                                p2p_store;
    boost::asio::io_service *                                       io_service;
    sem_t                                                           dataWritten;
    std::function<void(std::function<void()> const &, unsigned)>    scheduleAfter;
    bool                                                            fLogIPs;
    bool                                                            fDiscover;
    bool                                                            fListen;

    /** Subversion as sent to the P2P network in `version` messages */
    std::string                                                     strSubVersion;

    std::shared_ptr<CChainParams>                                   chainParams;

    /**
     * Return the currently selected parameters. This won't change after app
     * startup, except for unit tests.
     */
    const CChainParams &Params()
    {
        assert(chainParams);
        return *chainParams;
    }

private:
    using ListenSocket = std::shared_ptr<AsioServer>;

    bool BindListenPort(const CService &bindAddr, std::string& strError, bool fWhitelisted = false);
    bool Bind(const CService &addr, unsigned int flags);
    bool InitBinds(const std::vector<CService>& binds, const std::vector<CService>& whiteBinds);
    void ThreadOpenAddedConnections();
    void AddOneShot(const std::string& strDest);
    void ProcessOneShot();
    void ThreadOpenConnections(std::vector<std::string> connect);
    void ThreadMessageHandler();
    std::shared_ptr<CNode> AcceptConnection(std::shared_ptr<AsioSession> session, bool sock_whitelisted);
    bool AcceptReceivedBytes(std::shared_ptr<CNode> pnode, const char *pchBuf, int nBytes);
    void ThreadSocketHandler();
    void ThreadDNSAddressSeed();

    uint64_t CalculateKeyedNetGroup(const CAddress& ad) const;

    std::shared_ptr<CNode> FindNode(const CNetAddr& ip);
    std::shared_ptr<CNode> FindNode(const CSubNet& subNet);
    std::shared_ptr<CNode> FindNode(const std::string& addrName);
    std::shared_ptr<CNode> FindNode(const CService& addr);

    bool AttemptToEvictConnection();
    void ConnectNode(CAddress addrConnect, const char *pszDest, std::shared_ptr<CSemaphoreGrant> grantOutbound, int flags);
    std::shared_ptr<CNode> ConnectNodeFinish(AsioClient *client, std::shared_ptr<AsioSession> session);
    bool IsWhitelistedRange(const CNetAddr &addr);
    std::vector<CAddress> convertSeed6(const std::vector<SeedSpec6> &vSeedsIn);
    bool GetLocal(CService &addr, const CNetAddr *paddrPeer = nullptr);
    int GetnScore(const CService& addr);
    bool IsLimited(enum Network net);
    bool IsLimited(const CNetAddr& addr);
    void RemoveLocal(const CService& addr);
    bool IsLocal(const CService& addr);
    bool IsReachable(enum Network net);

    void DeleteNode(std::shared_ptr<CNode> pnode);

    NodeId GetNewNodeId();

    void SocketSendData(std::shared_ptr<CNode> pnode);
    bool SocketSendFinish(std::shared_ptr<CNode> pnode, int nBytes);
    //!check is the banlist has unwritten changes
    bool BannedSetIsDirty();
    //!set the "dirty" flag for the banlist
    void SetBannedSetDirty(bool dirty=true);
    //!clean unused entries (if bantime has expired)
    void SweepBanned();
    void DumpAddresses();
    void DumpBanlist();

    // Network stats
    void RecordBytesRecv(uint64_t bytes);
    void RecordBytesSent(uint64_t bytes);

    // Whether the node should be passed out in ForEach* callbacks
    static bool NodeFullyConnected(std::shared_ptr<CNode> pnode);

    // Network usage totals
    CCriticalSection                                                cs_totalBytesRecv;
    CCriticalSection                                                cs_totalBytesSent;
    uint64_t                                                        nTotalBytesRecv GUARDED_BY(cs_totalBytesRecv);
    uint64_t                                                        nTotalBytesSent GUARDED_BY(cs_totalBytesSent);

    // outbound limit & stats
    uint64_t                                                        nMaxOutboundTotalBytesSentInCycle GUARDED_BY(cs_totalBytesSent);
    uint64_t                                                        nMaxOutboundCycleStartTime GUARDED_BY(cs_totalBytesSent);
    uint64_t                                                        nMaxOutboundLimit GUARDED_BY(cs_totalBytesSent);
    uint64_t                                                        nMaxOutboundTimeframe GUARDED_BY(cs_totalBytesSent);

    // Whitelisted ranges. Any node connecting from these is automatically
    // whitelisted (as well as those connecting to whitelisted binds).
    std::vector<CSubNet>                                            vWhitelistedRange;

    unsigned int                                                    nSendBufferMaxSize;
    unsigned int                                                    nReceiveFloodSize;

    std::vector<ListenSocket>                                       vhListenSocket;
    std::atomic<bool>                                               fNetworkActive;
    banmap_t                                                        setBanned;
    CCriticalSection                                                cs_setBanned;
    bool                                                            setBannedIsDirty;
    bool                                                            fAddressesInitialized;
    CAddrMan                                                        addrman;
    std::deque<std::string>                                         vOneShots;
    CCriticalSection                                                cs_vOneShots;
    std::vector<std::string>                                        vAddedNodes GUARDED_BY(cs_vAddedNodes);
    CCriticalSection                                                cs_vAddedNodes;
    std::vector<std::shared_ptr<CNode>>                             vNodes;
    mutable CCriticalSection                                        cs_vNodes;
    std::atomic<NodeId>                                             nLastNodeId;

    /** Services this instance offers */
    CCriticalSection                                                cs_mapLocalHost;
    std::map<CNetAddr, LocalServiceInfo>                            mapLocalHost;
    bool                                                            vfLimited[NET_MAX];

    std::unique_ptr<CSemaphore>                                     semOutbound;
    std::unique_ptr<CSemaphore>                                     semAddnode;
    int                                                             nMaxConnections;
    int                                                             nMaxOutbound;
    int                                                             nMaxAddnode;
    int                                                             nMaxFeeler;
    NetEventsInterface*                                             m_msgproc;

    /** SipHasher seeds for deterministic randomness */
    const uint64_t                                                  nSeed0,
                                                                    nSeed1;

    /** flag for waking the message processor. */
    bool                                                            fMsgProcWake;

    std::condition_variable                                         condMsgProc;
    Mutex                                                           mutexMsgProc;
    std::atomic<bool>                                               flagInterruptMsgProc;

    CThreadInterrupt                                                interruptNet;

    std::thread                                                     threadDNSAddressSeed;
    std::thread                                                     threadSocketHandler;
    std::thread                                                     threadOpenAddedConnections;
    std::thread                                                     threadOpenConnections;
    std::thread                                                     threadMessageHandler;

    /** flag for deciding to connect to an extra outbound peer,
     *  in excess of nMaxOutbound
     *  This takes the place of a feeler connection */
    std::atomic_bool                                                m_try_another_outbound_peer;

    std::atomic<int64_t>                                            m_next_send_inv_to_incoming{0};

    friend struct CConnmanTest;
    friend class AsioClient;
    friend class AsioServer;
    friend class AsioSession;
    friend class p2p_internal;
};

bool BindListenPort(const CService &bindAddr, std::string& strError, bool fWhitelisted = false);

/**
 * Interface for message handling
 */
class NetEventsInterface
{
public:
    virtual bool ProcessMessages(std::shared_ptr<CNode> pnode, std::atomic<bool>& interrupt) = 0;
    virtual bool SendMessages(std::shared_ptr<CNode> pnode) = 0;
    virtual void InitializeNode(std::shared_ptr<CNode> pnode) = 0;
    virtual void FinalizeNode(NodeId id, bool& update_connection_time) = 0;

protected:
    /**
     * Protected destructor so that instances can only be deleted by derived classes.
     * If that restriction is no longer desired, this should be made public and virtual.
     */
    ~NetEventsInterface() = default;
};

typedef std::map<std::string, uint64_t> mapMsgCmdSize; //command, total bytes

class CNetMessage
{
private:
    mutable CHash256    hasher;
    mutable uint256     data_hash;
public:
    bool                in_data;         // parsing header (false) or data (true)

    CDataStream         hdrbuf;          // partially received header
    CMessageHeader      hdr;             // complete header
    unsigned int        nHdrPos;

    CDataStream         vRecv;           // received message data
    unsigned int        nDataPos;

    int64_t             nTime;           // time (in microseconds) of message receipt.

    CNetMessage(const CMessageHeader::MessageStartChars& pchMessageStartIn,
                int nTypeIn,
                int nVersionIn)
        : hdrbuf(nTypeIn, nVersionIn)
        , hdr(pchMessageStartIn)
        , vRecv(nTypeIn, nVersionIn)
    {
        hdrbuf.resize(24);
        in_data = false;
        nHdrPos = 0;
        nDataPos = 0;
        nTime = 0;
    }

    bool complete() const
    {
        if (!in_data)
            return false;
        return (hdr.nMessageSize == nDataPos);
    }

    const uint256& GetMessageHash() const;

    void SetVersion(int nVersionIn)
    {
        hdrbuf.SetVersion(nVersionIn);
        vRecv.SetVersion(nVersionIn);
    }

    int readHeader(const char *pch, unsigned int nBytes);
    int readData(const char *pch, unsigned int nBytes);
};

/** Information about a peer */
class CNode
{
    friend class CConnman;
public:
    // socket
    std::shared_ptr<AsioSession>            session;
    size_t                                  nSendSize; // total size of all vSendMsg entries
    uint64_t                                nSendBytes;
    std::deque<std::vector<unsigned char>>  vSendMsg;
    Mutex                                   cs_vSend;
    Mutex                                   cs_vRecv;

    Mutex                                   cs_vProcessMsg;
    std::list<CNetMessage>                  vProcessMsg;
    size_t                                  nProcessQueueSize;

    uint64_t                                nRecvBytes;
    std::atomic<int>                        nRecvVersion;

    std::atomic<int64_t>                    nLastSend;
    std::atomic<int64_t>                    nLastRecv;
    const int64_t                           nTimeConnected;
    std::atomic<int64_t>                    nTimeOffset;
    // Address of this peer
    const CAddress                          addr;
    // Bind address of our side of the connection
    const CAddress                          addrBind;
    std::atomic<int>                        nVersion;
    // strSubVer is whatever byte array we read from the wire. However, this field is intended
    // to be printed out, displayed to humans in various forms and so on. So we sanitize it and
    // store the sanitized version in cleanSubVer. The original should be used when dealing with
    // the network or wire types and the cleaned string used when displayed or logged.
    std::string                             strSubVer,
                                            cleanSubVer;
    Mutex                                   cs_SubVer; // used for both cleanSubVer and strSubVer
    bool                                    fWhitelisted; // This peer can bypass DoS banning.
    bool                                    fFeeler; // If true this node is being used as a short lived feeler.
    bool                                    fOneShot;
    bool                                    m_manual_connection;
    const bool                              fInbound;
    std::atomic_bool                        fSuccessfullyConnected;
    std::atomic_bool                        fDisconnect;
    bool                                    fSentAddr;
    CSemaphoreGrant                         grantOutbound;

    const uint64_t                          nKeyedNetGroup;
    std::atomic_bool                        fPauseRecv;
    std::atomic_bool                        fPauseSend;
    uint64_t                                next_propagate_index;
    std::atomic_bool                        sendCompleted;
protected:

    mapMsgCmdSize                           mapSendBytesPerMsgCmd;
    mapMsgCmdSize                           mapRecvBytesPerMsgCmd;

public:
    uint256                                 hashContinue;

    // flood relay
    std::vector<CAddress>                   vAddrToSend;
    CRollingBloomFilter                     addrKnown;
    bool                                    fGetAddr;
    std::set<uint256>                       setKnown;
    int64_t                                 nNextAddrSend;
    int64_t                                 nNextLocalAddrSend;

    // Ping time measurement:
    // The pong reply we're expecting, or 0 if no pong expected.
    std::atomic<uint64_t>                   nPingNonceSent;
    // Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
    std::atomic<int64_t>                    nPingUsecStart;
    // Last measured round-trip time.
    std::atomic<int64_t>                    nPingUsecTime;
    // Best measured round-trip time.
    std::atomic<int64_t>                    nMinPingUsecTime;
    // Whether a ping is requested.
    std::atomic<bool>                       fPingQueued;

    CNode(NodeId id,
          std::shared_ptr<AsioSession> sessionIn,
          const CAddress &addrIn,
          uint64_t nKeyedNetGroupIn,
          uint64_t nLocalHostNonceIn,
          const CAddress &addrBindIn,
          const std::string &addrNameIn = "",
          bool fInboundIn = false);
    ~CNode();
    CNode(const CNode&) = delete;
    CNode& operator=(const CNode&) = delete;

private:
    const NodeId                            id;
    const uint64_t                          nLocalHostNonce;
    int                                     nSendVersion;
    std::list<CNetMessage>                  vRecvMsg;  // Used only by SocketHandler thread

    mutable CCriticalSection                cs_addrName;
    std::string                             addrName;

    // Our address, as reported by the peer
    CService                                addrLocal;
    mutable Mutex                           cs_addrLocal;
    BCLog::Logger &                         logger_;
public:
    CConnman &                              connman;

    NodeId GetId() const
    {
        return id;
    }

    uint64_t GetLocalNonce() const
    {
        return nLocalHostNonce;
    }

    bool ReceiveMsgBytes(const char *pch, unsigned int nBytes, bool& complete);

    void SetRecvVersion(int nVersionIn)
    {
        nRecvVersion = nVersionIn;
    }
    int GetRecvVersion() const
    {
        return nRecvVersion;
    }
    void SetSendVersion(int nVersionIn);
    int GetSendVersion() const;

    CService GetAddrLocal() const;
    //! May not be called more than once
    void SetAddrLocal(const CService& addrLocalIn);


    void AddAddressKnown(const CAddress& _addr)
    {
        addrKnown.insert(_addr.GetKey());
    }

    void PushAddress(const CAddress& _addr, FastRandomContext &insecure_rand)
    {
        // Known checking here is only to save space from duplicates.
        // SendMessages will filter it again for knowns that were added
        // after addresses were pushed.
        if (_addr.IsValid() && !addrKnown.contains(_addr.GetKey()))
        {
            if (vAddrToSend.size() >= MAX_ADDR_TO_SEND)
                vAddrToSend[insecure_rand.randrange(vAddrToSend.size())] = _addr;
            else
                vAddrToSend.push_back(_addr);
        }
    }

    void CloseSocketDisconnect();

    std::string GetAddrName() const;
    //! Sets the addrName only if it was not previously set
    void MaybeSetAddrName(const std::string& addrNameIn);
    friend class AsioSession;
};

#endif // BITCOIN_NET_H
