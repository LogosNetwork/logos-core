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

template<ConsensusType type>
inline std::string ConsensusToName()
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
            return "Contains Invalid Request";
        case RejectionReason::Invalid_Previous_Hash:
            return "Invalid Previous Hash";
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

//for debug
std::string to_string (const std::vector<uint8_t> & buf);

//namespace logos
//{
//
//
//bool read (logos::stream & stream_a, std::vector<bool> & value);
////{
////    uint16_t n_bits_le = 0;
////    bool error = logos::read(stream_a, n_bits_le);
////    if(error)
////    {
////        return error;
////    }
////    auto n_bits = le16toh(n_bits_le);
////    auto to_read = int_ceiling(n_bits);
////
////    std::vector<uint8_t> bytes(to_read);
////    auto amount_read (stream_a.sgetn (bytes.data(), bytes.size()));
////    if(amount_read != to_read)
////    {
////        return false;
////    }
////
////    for( auto b : bytes)
////    {
////        for(int i = 0; i < 8; ++i)
////        {
////            uint8_t mask = !(1<<i);
////            if(mask & b)
////                value.push_back(true);
////            else
////                value.push_back(false);
////        }
////    }
////
////    return true;
////}
//
//uint32_t write (logos::stream & stream_a, const std::vector<bool> & value);
////{
////    assert(value.size() <= int_ceiling(CONSENSUS_BATCH_SIZE));
////    uint16_t n_bits = value.size();
////    auto n_bits_le = htole16(n_bits);
////
////    auto amount_written (stream_a.sputn ((uint8_t *)&n_bits_le, sizeof(uint16_t)));
////    std::vector<uint8_t> buf;
////    uint8_t one_byte = 0;
////    int cmp = 0;
////    for ( auto b : value)
////    {
////        one_byte = one_byte | ((b ? 1 : 0) << cmp++);
////        if(cmp == 8)
////        {
////            buf.push_back(one_byte);
////            cmp = 0;
////            one_byte = 0;
////        }
////    }
////    if(cmp != 0)
////    {
////        buf.push_back(one_byte);
////    }
////    amount_written += stream_a.sputn (buf.data(), buf.size());
////    return amount_written;
////}
//
//}
//
