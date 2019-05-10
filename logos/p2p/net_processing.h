// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include <net.h>

/** Default for BIP61 (sending reject messages) */
constexpr bool DEFAULT_ENABLE_BIP61 = true;

class PeerLogicValidation_internal;

class PeerLogicValidation final : public NetEventsInterface
{
private:
    CConnman* const                                 connman;
    BCLog::Logger &                                 logger_;
    std::shared_ptr<PeerLogicValidation_internal>   internal;

public:
    explicit PeerLogicValidation(CConnman* connman, bool enable_bip61);

    /** Initialize a peer by adding it to mapNodeState and pushing a message requesting its version */
    void InitializeNode(std::shared_ptr<CNode> pnode) override;
    /** Handle removal of a peer by updating various state and removing it from mapNodeState */
    void FinalizeNode(NodeId nodeid, bool& fUpdateConnectionTime) override;
    /**
    * Process protocol messages received from a given node
    *
    * @param[in]   pfrom           The node which we have received messages from.
    * @param[in]   interrupt       Interrupt condition for processing threads
    */
    bool ProcessMessages(std::shared_ptr<CNode> pfrom, std::atomic<bool>& interrupt) override;
    /**
    * Send queued protocol messages to be sent to a give node.
    *
    * @param[in]   pto             The node which we are sending messages to.
    * @return                      True if there is more work to be done
    */
    bool SendMessages(std::shared_ptr<CNode> pto) override;

    /** Evict extra outbound peers. If we think our tip may be stale, connect to an extra outbound */
    void CheckForStaleTipAndEvictPeers(int nPowTargetSpacing);
    /** If we have extra outbound peers, try to disconnect the one with the oldest block announcement */
    void EvictExtraOutboundPeers(int64_t time_in_seconds);

private:
    bool TipMayBeStale(int nPowTargetSpacing) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    int64_t                                         m_stale_tip_check_time; //! Next time to check for stale tip

    /** Enable BIP61 (sending reject messages) */
    const bool                                      m_enable_bip61;
};

#endif // BITCOIN_NET_PROCESSING_H
