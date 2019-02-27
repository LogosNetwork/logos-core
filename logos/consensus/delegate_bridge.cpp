// @file
// Implementes DelegateBridge class
//

#include <logos/node/delegate_identity_manager.hpp>
#include <logos/network/consensus_netio.hpp>
#include <logos/consensus/delegate_bridge.hpp>
#include <logos/consensus/messages/util.hpp>

template<ConsensusType CT>
DelegateBridge<CT>::DelegateBridge(Service & service,
                                   std::shared_ptr<IOChannel> iochannel,
                                   p2p_interface & p2p,
                                   uint8_t delegate_id)
    : ConsensusMsgSink(service)
    , ConsensusP2pBridge<CT>(service, p2p, delegate_id)
    , _iochannel(iochannel)
{}

template<ConsensusType CT>
void DelegateBridge<CT>::Send(const void * data, size_t size)
{
#define P2PTEST
#ifdef P2PTEST
    // simulate network send failure
    struct stat sb;
    std::string path = "./DB/Consensus_" +
                       std::to_string((int) DelegateIdentityManager::_global_delegate_idx) +
                       "/sndoff";
    if (stat(path.c_str(), &sb) == 0 && (sb.st_mode & S_IFMT) == S_IFREG)
    {
        return;
    }
#endif
    _iochannel->Send(data, size);
}

template<ConsensusType CT>
std::shared_ptr<MessageBase>
DelegateBridge<CT>::Parse(const uint8_t * data, uint8_t version, MessageType message_type,
                                   ConsensusType consensus_type, uint32_t payload_size)
{
    logos::bufferstream stream(data, payload_size);
    std::shared_ptr<MessageBase> msg = nullptr;
    bool error = false;

    switch (message_type)
    {
        case MessageType::Pre_Prepare: {
            msg = std::make_shared<PrePrepare>(error, stream, version);
            break;
        }
        case MessageType::Prepare: {
            msg = std::make_shared<Prepare>(error, stream, version);
            break;
        }
        case MessageType::Post_Prepare: {
            msg = std::make_shared<PostPrepare>(error, stream, version);
            break;
        }
        case MessageType::Commit: {
            msg = std::make_shared<Commit>(error, stream, version);
            break;
        }
        case MessageType::Post_Commit: {
            msg = std::make_shared<PostCommit>(error, stream, version);
            break;
        }
        case MessageType::Rejection: {
            msg = std::make_shared<Rejection>(error, stream, version);
            break;
        }
        default:
            return nullptr;
    }

    if (!error)
    {
        return msg;
    }
    else
    {
        LOG_ERROR(_log) << "DelegateBridge::Parser, failed to deserialize";
        return nullptr;
    }
}

template<ConsensusType CT>
void DelegateBridge<CT>::OnMessage(std::shared_ptr<MessageBase> message, MessageType message_type, bool is_p2p)
{
    bool error = false;
    auto log_message_received ([&](const std::string & msg_str, const std::string & hash_str){
        LOG_DEBUG(_log) << "DelegateBridge<" << ConsensusToName(CT) << "> - Received "
                        << msg_str << " message from delegate: " << (int)RemoteDelegateId()
                        << " with block hash " << hash_str
                        << " via direct connection " << (!is_p2p);
    });
    switch (message_type)
    {
        case MessageType::Pre_Prepare:
        {
            this->EnableP2p(is_p2p);
            auto msg = dynamic_pointer_cast<PrePrepare>(message);
            log_message_received(MessageToName(message_type), msg->Hash().to_string());
            OnConsensusMessage(*msg);
            break;
        }
        case MessageType::Prepare:
        {
            auto msg = dynamic_pointer_cast<Prepare>(message);
            log_message_received(MessageToName(message_type), msg->preprepare_hash.to_string());
            OnConsensusMessage(*msg);
            break;
        }
        case MessageType::Post_Prepare:
        {
            this->EnableP2p(is_p2p);
            auto msg = dynamic_pointer_cast<PostPrepare>(message);
            log_message_received(MessageToName(message_type), msg->preprepare_hash.to_string());
            OnConsensusMessage(*msg);
            break;
        }
        case MessageType::Commit:
        {
            auto msg = dynamic_pointer_cast<Commit>(message);
            log_message_received(MessageToName(message_type), msg->preprepare_hash.to_string());
            OnConsensusMessage(*msg);
            break;
        }
        case MessageType::Post_Commit:
        {
            this->EnableP2p(is_p2p);
            auto msg = dynamic_pointer_cast<PostCommit>(message);
            log_message_received(MessageToName(message_type), msg->preprepare_hash.to_string());
            OnConsensusMessage(*msg);
            break;
        }
        case MessageType::Rejection:
        {
            auto msg = dynamic_pointer_cast<Rejection>(message);
            auto msg_str (MessageToName(message_type) + ":" + RejectionReasonToName(msg->reason));
            log_message_received(msg_str, msg->preprepare_hash.to_string());
            OnConsensusMessage(*msg);
            break;
        }
        case MessageType::Post_Committed_Block:
            // will not receive Post_Committed_Block
        case MessageType::Heart_Beat:
        case MessageType::Key_Advert:
        case MessageType::TxAcceptor_Message:
        case MessageType::Unknown:
        {
            LOG_WARN(_log) << "DelegateBridge<" << ConsensusToName(CT) << "> - Received "
                            << MessageToName(message_type) << " message from delegate: "
                            << (int)RemoteDelegateId();
            error = true;
            break;
        }
    }
}

template class DelegateBridge<ConsensusType::BatchStateBlock>;
template class DelegateBridge<ConsensusType::MicroBlock>;
template class DelegateBridge<ConsensusType::Epoch>;
