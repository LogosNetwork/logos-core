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
        case MessageType::Post_Committed_Block:
            ret = "Post_Committed_Block";
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
        case ConsensusType::Request:
            ret = "RequestBlock";
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
    std::string result;

    switch (reason)
    {
        case RejectionReason::Void:
            result = "Void";
            break;
        case RejectionReason::Contains_Invalid_Request:
            result = "Contains Invalid Request";
            break;
        case RejectionReason::Clock_Drift:
            result = "Clock Drift";
            break;
        case RejectionReason::Bad_Signature:
            result = "Bad Signature";
            break;
        case RejectionReason::Invalid_Epoch:
            result = "Invalid Epoch";
            break;
        case RejectionReason::New_Epoch:
            result = "New Epoch";
            break;
        case RejectionReason::Wrong_Sequence_Number:
            result = "Wrong Sequence Number";
            break;
        case RejectionReason::Invalid_Previous_Hash:
            result = "Invalid Previous Hash";
            break;
        case RejectionReason::Invalid_Primary_Index:
            result = "Invalid Primary Index";
            break;
        default:
            assert(0);
    }

    return result;
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

inline
std::ostream& operator<<(std::ostream &os, P2pAppType at)
{
    switch (at)
    {
        case P2pAppType::Consensus:
            os << "Consensus";
            break;
        case P2pAppType::AddressAd:
            os << "AddressAd";
            break;
        case P2pAppType::AddressAdTxAcceptor:
            os << "AddressAdTxAcceptor";
            break;
    }
    return os;
}


