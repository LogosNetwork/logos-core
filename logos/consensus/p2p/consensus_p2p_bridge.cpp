// @file
// ConsensusP2pBridge p2p interface bridge to ConsensusManager and BackupDelegate
//

#include <logos/consensus/p2p/consensus_p2p_bridge.hpp>
#include <logos/consensus/messages/common.hpp>

//when p2p consensus is enabled, the p2p subsystem is flooded, and experiences
//higher traffic than any other time. Usually, one round of consensus results 
//in a single p2p message (the post-committed block). When p2p consensus is
//enabled, one round of consensus results in:
//1 PrePrepare + 31 Prepare + 1 PostPrepare + 31 Commit + 1 PostCommit +
//1 Post Committed Block = 66 p2p messages
//Therefore, we should turn off p2p consensus as quick as we can
const boost::posix_time::seconds ConsensusP2pBridge::P2P_TIMEOUT{10};

ConsensusP2pBridge::ConsensusP2pBridge(Service &service, p2p_interface &p2p, uint8_t delegate_id)
    : _p2p_output(p2p, delegate_id)
    , _enable_p2p(false)
    , _timer(service)
{}

bool
ConsensusP2pBridge::Broadcast(const uint8_t *data, uint32_t size, MessageType message_type)
{
    return _p2p_output.ProcessOutputMessage(data, size, message_type, 0, 0xff);
}

bool
ConsensusP2pBridge::SendP2p(const uint8_t *data, uint32_t size, MessageType message_type,
                        uint32_t epoch_number, uint8_t dest_delegate_id)
{
    if (_enable_p2p)
    {
        return _p2p_output.ProcessOutputMessage(data, size, message_type, epoch_number, dest_delegate_id);
    }
    return true;
}

void
ConsensusP2pBridge::ScheduleP2pTimer(TimeoutCb ontimeout, ConsensusP2pBridge::Seconds s)
{
    _timer.expires_from_now(s);
    _timer.async_wait(ontimeout);
}
