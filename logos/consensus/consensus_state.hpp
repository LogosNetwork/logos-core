#pragma once

#include <cstdint>
#include <string>

enum class ConsensusState : uint8_t
{
    PRE_PREPARE = 0,
    PREPARE,
    POST_PREPARE,
    COMMIT,
    POST_COMMIT,

    VOID,
    RECALL
};

inline
std::string StateToString(ConsensusState state)
{
    std::string ret;

    switch(state)
    {
        case ConsensusState::PRE_PREPARE:
            ret = "Pre Prepare";
            break;
        case ConsensusState::PREPARE:
            ret = "Prepare";
            break;
        case ConsensusState::POST_PREPARE:
            ret = "Post Prepare";
            break;
        case ConsensusState::COMMIT:
            ret = "Commit";
            break;
        case ConsensusState::POST_COMMIT:
            ret = "Post Commit";
            break;
        case ConsensusState::VOID:
            ret = "Void";
            break;
        case ConsensusState::RECALL:
            ret = "Recall";
            break;
    }

    return ret;
}
