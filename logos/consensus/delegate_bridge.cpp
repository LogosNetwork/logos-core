// @file
// Implementes DelegateBridge class
//

#include <logos/consensus/network/consensus_netio.hpp>
#include <logos/consensus/delegate_bridge.hpp>
#include <logos/consensus/messages/util.hpp>

template<ConsensusType CT>
DelegateBridge<CT>::DelegateBridge(std::shared_ptr<IOChannel> iochannel)
    : _iochannel(iochannel)
{}

template<ConsensusType CT>
void DelegateBridge<CT>::Send(const void * data, size_t size)
{
    _iochannel->Send(data, size);
}

template<ConsensusType CT>
bool DelegateBridge<CT>::OnMessageData(const uint8_t * data,
        uint8_t version,
        MessageType message_type,
        ConsensusType consensus_type,
        uint32_t payload_size)
{
    LOG_DEBUG(_log) << "DelegateBridge<"
                    << ConsensusToName(CT) << ">- Received "
                    << MessageToName(message_type)
                    << " message from delegate: " << (int)RemoteDelegateId();

    bool error = false;
    logos::bufferstream stream(data, payload_size);
    switch (message_type)
    {
        case MessageType::Pre_Prepare:
        {
            PrePrepare msg(error, stream, version);
            if(!error)
                OnConsensusMessage(msg);
            break;
        }
        case MessageType::Prepare:
        {
            Prepare msg(error, stream, version);
            if(!error)
                OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Prepare:
        {
            PostPrepare msg(error, stream, version);
            if(!error)
                OnConsensusMessage(msg);
            break;
        }
        case MessageType::Commit:
        {
            Commit msg(error, stream, version);
            if(!error)
                OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Commit:
        {
            PostCommit msg(error, stream, version);
            if(!error)
                OnConsensusMessage(msg);
            break;
        }
        case MessageType::Rejection:
        {
            Rejection msg (error, stream, version);
            if(!error)
                OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Committed_Block:
            // will not receive Post_Committed_Block
        case MessageType::Heart_Beat:
        case MessageType::Key_Advert:
        case MessageType::Unknown:
            error = true;
            break;
    }

    if(error)
        LOG_ERROR(_log) << __func__ << " message error";

    return ! error;
}

template class DelegateBridge<ConsensusType::BatchStateBlock>;
template class DelegateBridge<ConsensusType::MicroBlock>;
template class DelegateBridge<ConsensusType::Epoch>;
