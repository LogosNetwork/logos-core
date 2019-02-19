// @file
// ConsensusP2pBridge p2p interface bridge to ConsensusManager and BackupDelegate
//
#pragma once

#include <logos/consensus/p2p/consensus_p2p.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>

#include <atomic>

class p2p_interface;

template<ConsensusType CT>
class ConsensusP2pBridge {
    using Timer         = boost::asio::deadline_timer;
    using ErrorCode     = boost::system::error_code;
    using Service       = boost::asio::io_service;

public:
    ConsensusP2pBridge(Service &service, p2p_interface & p2p, uint8_t delegate_id);
    virtual ~ConsensusP2pBridge() = default;

protected:
    bool Broadcast(const uint8_t *data, uint32_t size);
    virtual void OnP2pTimeout(const ErrorCode & ec) {}
    void Enable(bool enable)
    {
        _enable_p2p = enable;
    }
    bool SendP2p(const uint8_t *data, uint32_t size, uint32_t epoch_number, uint8_t dest_delegate_id);
    p2p_interface & GetP2p() { return _p2p_output._p2p; }
    bool P2pEnabled()
    {
        _enable_p2p;
    }

private:
    ConsensusP2pOutput<CT>  _p2p_output;
    atomic_bool             _enable_p2p;    /// Enable p2p flag for p2p backup consensus
    Log                     _log;
    Timer                   _timer;
};
