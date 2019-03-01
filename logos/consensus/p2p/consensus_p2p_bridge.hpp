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

/// Bridge class between P2p and ConsensusManager/DelegateBridge
template<ConsensusType CT>
class ConsensusP2pBridge {
    using Timer         = boost::asio::deadline_timer;
    using ErrorCode     = boost::system::error_code;
    using Service       = boost::asio::io_service;
    using Seconds       = boost::posix_time::seconds;
    using TimeoutCb     = std::function<void(const ErrorCode&)>;

public:
    /// Class constructor
    /// @param service boost asio sevice
    /// @param p2p reference to p2p object
    /// @param delegate_id remote delegate id
    ConsensusP2pBridge(Service &service, p2p_interface & p2p, uint8_t delegate_id);
    /// Class destructor
    virtual ~ConsensusP2pBridge() = default;

protected:
    /// Broadcast message to all peers via p2p
    /// @param data buffer
    /// @param size buffer size
    /// @param message_type being broadcasted
    /// @returns true on success
    bool Broadcast(const uint8_t *data, uint32_t size, MessageType message_type);
    /// P2p timer to check if p2p should be enabled/disabled
    /// @param ec timer error code
    virtual void OnP2pTimeout(const ErrorCode & ec) {}
    /// Enable/disable p2p
    /// @param enable true to enable, false otherwise
    virtual void EnableP2p(bool enable)
    {
        _enable_p2p = enable;
    }
    /// Send via p2p to the designated delegate. The message is broadcasted to all peers
    /// but the delegates filter out the message not designated to them
    /// dest_delegate_id = 0xff is the same as broadcast
    /// @param data buffer
    /// @param size buffer size
    /// @param message_type being broadcasted
    /// @param epoch_number current epoch number used for filtering
    /// @param dest_delegate_id destination delegate id used for filtering
    /// @returns true on success
    virtual bool SendP2p(const uint8_t *data, uint32_t size, MessageType message_type,
                         uint32_t epoch_number, uint8_t dest_delegate_id);
    /// Get p2p interface
    /// @returns p2p interface reference
    p2p_interface & GetP2p() { return _p2p_output._p2p; }
    /// Is p2p enabled
    /// @returns true if p2p is enabled
    bool P2pEnabled()
    {
        _enable_p2p;
    }

    /// Schedule p2p timer
    /// @param timeout_cb timeout callback
    /// @param s time lapse value
    void ScheduleP2pTimer(TimeoutCb timeout_cb, Seconds s = P2P_TIMEOUT);

private:
    static const Seconds    P2P_TIMEOUT;    /// p2p default timeout 60 seconds
    ConsensusP2pOutput<CT>  _p2p_output;    /// p2p object which handles p2p receive/send
    atomic_bool             _enable_p2p;    /// Enable p2p flag for p2p backup consensus
    Log                     _log;           /// Log object
    Timer                   _timer;         /// P2p timer object
};
