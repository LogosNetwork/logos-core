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
    using Handle        = uint64_t;
public:
    /// Class constructor
    /// @param alarm logos::alarm reference [in]
    /// @param recall_handler recall handler reference [in]
    EventProposer(logos::alarm &, IRecallHandler &recall_handler);
    ~EventProposer() = default;

    /// Generates periodic event to propose microblock
    /// @param mcb callback to call when the event occurs [in]
    /// @param skip_micro_block whether to skip an interval for the first MB proposal at genesis [in]
    void ProposeMicroBlock(MicroCb mcb, bool skip_micro_block = false);

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

    /// Start epoch transition event loop
    /// @param tcb callback to call when the epoch transition event occurs [in]
    /// @param first_epoch whether to skip an interval for the first Epoch Transition at genesis [in]
    void Start(TransitionCb tcb, bool first_epoch);

    /// Start microblock and epoch block proposal loop
    /// @param mcb callback to call when the microblock event occurs [in]
    /// @param ecb callback to call when the epoch should be proposed [in]
    /// @param first_microblock whether to skip an interval for the first MB proposal at genesis [in]
    void StartArchival(MicroCb mcb, EpochCb ecb, bool first_microblock);

    void StopArchival();

private:
    // number of intervals to skip on first microblock after genesis microblock
    const uint8_t       FIRST_MICROBLOCK_SKIP = 2;
    const Handle        CANCELLED = -1;
    std::mutex          _mutex;            ///< for protecting alarm access
    logos::alarm &      _alarm;            ///< logos::alarm reference
    Handle              _mb_handle    ;    ///< handle to keep track of scheduled MB proposal in the future
    EpochCb             _epoch_cb;         ///< delayed epoch call back
    Log                 _log;              ///< boost asio log
    bool                _skip_transition;  ///< skip first Epoch transition due time
    IRecallHandler &    _recall_handler;   ///< recall handler reference
};