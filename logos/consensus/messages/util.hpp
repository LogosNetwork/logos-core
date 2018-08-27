#pragma once

#include <logos/consensus/messages/common.hpp>

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
        case MessageType::Unknown:
            ret = "Unknown";
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
            ret = "BatchStateBlock";
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
    return ConsensusToName(message.type);
}

inline size_t ConsensusTypeToIndex(ConsensusType type)
{
    return uint64_t(static_cast<uint8_t>(type));
}
