#pragma once

#include <logos/consensus/messages/rejection.hpp>

#include <string>

inline std::string MessageToName(const MessageType & type)
{
    std::string ret;

    switch(type)
    {
        case MessageType::Pre_Prepare:
            ret = "Pre_Prepare";
            break;
        case MessageType::Prepare:
            ret = "Prepare";
            break;
        case MessageType::Post_Prepare:
            ret = "Post_Prepare";
            break;
        case MessageType::Commit:
            ret = "Commit";
            break;
        case MessageType::Post_Commit:
            ret = "Post_Commit";
            break;
        case MessageType::Key_Advert:
            ret = "Key Advertisement";
            break;
        case MessageType::Rejection:
            ret = "Rejection";
            break;
        case MessageType::Unknown:
            ret = "Unknown";
            break;
        case MessageType::Heart_Beat:
            ret = "Heart Beat";
            break;
        case MessageType::TxAcceptor_Message:
            ret = "TxAcceptor Message";
            break;
        default:
            ret = "Undefined";
    }

    return ret;
}

inline std::string ConsensusToName(const ConsensusType & type)
{
    std::string ret;
    switch (type)
    {
        case ConsensusType::BatchStateBlock:
            ret = "BatchStateBlock";
            break;
        case ConsensusType::MicroBlock:
            ret = "MicroBlock";
            break;
        case ConsensusType::Epoch:
            ret = "Epoch";
            break;
        case ConsensusType::Any:
            ret = "Any";
            break;
        default:
            ret = "Undefined";
    }

    return ret;
}

template<typename MSG>
std::string MessageToName(const MSG & message)
{
    return MessageToName(message.type);
}

template<typename MSG>
std::string ConsensusToName(const MSG & message)
{
    return ConsensusToName(message.consensus_type);
}

inline size_t ConsensusTypeToIndex(ConsensusType type)
{
    size_t index = uint64_t(static_cast<uint8_t>(type));
    assert(index < CONSENSUS_TYPE_COUNT);
    return index;
}

inline std::string RejectionReasonToName(RejectionReason reason)
{
    switch (reason)
    {
        case RejectionReason::Void:
            return "Void";
        case RejectionReason::Contains_Invalid_Request:
            return "Contains Invalid Request";
        case RejectionReason::Clock_Drift:
            return "Clock Drift";
        case RejectionReason::Bad_Signature:
            return "Bad Signature";
        case RejectionReason::Invalid_Epoch:
            return "Invalid Epoch";
        case RejectionReason::New_Epoch:
            return "New Epoch";
        case RejectionReason::Wrong_Sequence_Number:
            return "Wrong Sequence Number";
        case RejectionReason::Invalid_Previous_Hash:
            return "Invalid Previous Hash";
        case RejectionReason::Invalid_Primary_Index:
            return "Invalid Primary Index";
        default:
            assert(0);
    }
}

template<MessageType MT, ConsensusType CT>
std::ostream& operator<<(std::ostream& os, const MessagePrequel<MT, CT>& m)
{
    os << "version: " << int(m.version)
       << " type: " << MessageToName(m.type)
       << " consensus_type: " << ConsensusToName(m.consensus_type);

    return os;
}

template<ConsensusType CT>
std::ostream& operator<<(std::ostream& os, const RejectionMessage<CT>& m)
{
    os << static_cast<MessagePrequel<MessageType::Rejection, CT> &>(m);
    return os;
}



