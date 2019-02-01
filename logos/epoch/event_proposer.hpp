///
/// @file
/// This file contains declaration of the EventProposer, which proposes epoch related events
///

#pragma once


#include <logos/lib/epoch_time_util.hpp>
#include <logos/lib/log.hpp>

#include <functional>

namespace logos
{
    class alarm;
}

class MicroBlock;
class IRecallHandler;

/// Defines functions that propose microblock and transition events
class EventProposer
{
    using MicroCb       = std::function<void()>;
    using TransitionCb  = std::function<void()>;
    using EpochCb       = std::function<void()>;
public:
    /// Class constructor
    /// @param alarm logos::alarm reference [in]
    /// @param recall_handler recall handler reference [in]
    /// @param first_epoch is it first epoch [in]
    /// @param first_microblock is it first epoch [in]
    EventProposer(logos::alarm &, IRecallHandler &recall_handler,
                  bool first_epoch, bool first_microblock);
    ~EventProposer() = default;

    /// Generates periodic event to propose microblock
    /// @param mcb callback to call when the event occurs [in]
    void ProposeMicroBlock(MicroCb mcb);

    /// Generates one off event to propose microblock
    /// @param mcb callback to call when the event occurs [in]
    /// @param seconds generate event seconds from now [in]
    void ProposeMicroBlockOnce(MicroCb mcb, std::chrono::seconds seconds = std::chrono::seconds(0));

    /// Generates periodic event to propose epoch transition
    /// @param tcb callback to call when the event occurs [in]
    /// @param next skip to next epoch transition
    void ProposeTransition(TransitionCb tcb, bool next = false);

    /// Generates one off event to propose epoch transition
    /// @param tcb callback to call when the event occurs [in]
    /// @param seconds generate event seconds from now [in]
    void ProposeTransitionOnce(TransitionCb tcb, std::chrono::seconds seconds = std::chrono::seconds(0));

    /// Generates one off event to propose epoch transition
    /// @param ecb callback to call when the event occurs [in]
    void ProposeEpoch();

    /// Start microblock and epoch transition event loop
    /// @param mcb callback to call when the microblock event occurs [in]
    /// @param tcb callback to call when the epoch transition event occurs [in]
    /// @param ecb callback to call when the epoch should be proposed [in]
    void Start(MicroCb mcb, TransitionCb tcb, EpochCb ecb);
private:
    // number of intervals to skip on first microblock after genesis microblock
    const uint8_t FIRST_MICROBLOCK_SKIP = 2;
    logos::alarm &      _alarm;            ///< logos::alarm reference
    EpochCb             _epoch_cb;         ///< delayed epoch call back
    Log                 _log;              ///< boost asio log
    bool                _skip_transition;  ///< skip first Epoch transition due time
    bool                _skip_micro_block; ///< skip first MicroBlock transition due time
    IRecallHandler &    _recall_handler;   ///< recall handler reference
};