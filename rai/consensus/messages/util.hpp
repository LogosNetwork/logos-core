#pragma once

#include <rai/consensus/messages/common.hpp>

#include <string>

inline std::string MessageToName(MessageType type)
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
    }

    return ret;
}

template<typename MSG>
std::string MessageToName(const MSG & message)
{
    return MessageToName(message.type);
}
