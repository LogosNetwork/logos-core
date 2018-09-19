///
/// @file
/// This file contains declaration of the EventProposer, which proposes epoch related events
///

#pragma once


#include <logos/lib/epoch_time_util.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <functional>

namespace logos
{
    class alarm;
}

class MicroBlock;

/// Defines functions that propose microblock and transition events
class EventProposer
{
    using Log           = boost::log::sources::logger_mt;
    using MicroCb       = std::function<void()>;
    using TransitionCb  = std::function<void()>;
    using EpochCb       = std::function<void()>;
public:
    /// Class constructor
    /// @param alarm logos::alarm reference [in]
    EventProposer(logos::alarm &, bool first_epoch);
    ~EventProposer() = default;

    /// Generates periodic event to propose microblock
    /// @param mcb callback to call when the event occurs [in]
    void ProposeMicroblock(MicroCb mcb);

    /// Generates one off event to propose microblock
    /// @param mcb callback to call when the event occurs [in]
    /// @param seconds generate event seconds from now [in]
    void ProposeMicroblockOnce(MicroCb mcb, std::chrono::seconds seconds = std::chrono::seconds(0));

    /// Generates periodic event to propose epoch transition
    /// @param tcb callback to call when the event occurs [in]
    void ProposeTransition(TransitionCb tcb);

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
    logos::alarm &      _alarm;            ///< logos::alarm reference
    EpochCb             _epoch_cb;         ///< delayed epoch call back
    Log                 _log;              ///< boost asio log
    bool                _skip_transition;  ///< skip first Epoch transition due time
    bool                _skip_micro_block; ///< skip first MicroBlock transition due time
};