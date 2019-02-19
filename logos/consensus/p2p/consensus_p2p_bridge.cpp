// @file
// ConsensusP2pBridge p2p interface bridge to ConsensusManager and BackupDelegate
//

#include <logos/consensus/p2p/consensus_p2p_bridge.hpp>

template<ConsensusType CT>
ConsensusP2pBridge<CT>::ConsensusP2pBridge(Service &service, p2p_interface &p2p, uint8_t delegate_id)
    : _p2p_output(p2p, delegate_id)
    , _enable_p2p(true)
    , _timer(service)
{}

template<ConsensusType CT>
bool
ConsensusP2pBridge<CT>::Broadcast(const uint8_t *data, uint32_t size)
{
    return _p2p_output.ProcessOutputMessage(data, size, 0, 0xff);
}

template<ConsensusType CT>
bool
ConsensusP2pBridge<CT>::SendP2p(const uint8_t *data, uint32_t size, uint32_t epoch_number, uint8_t dest_delegate_id)
{
    if (_enable_p2p)
    {
        return _p2p_output.ProcessOutputMessage(data, size, epoch_number, dest_delegate_id);
    }
    return true;
}

template class ConsensusP2pBridge<ConsensusType::BatchStateBlock>;
template class ConsensusP2pBridge<ConsensusType::MicroBlock>;
template class ConsensusP2pBridge<ConsensusType::Epoch>;