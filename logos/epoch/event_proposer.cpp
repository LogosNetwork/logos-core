//
// Created by gregt on 9/10/18.
//

#include <logos/epoch/event_proposer.hpp>
#include <logos/node/node.hpp>

EventProposer::EventProposer(logos::alarm & alarm)
    : _alarm(alarm)
    {}

void
EventProposer::Start(
    MicroCb mcb,
    TransitionCb tcb,
    EpochCb ecb)
{
    ProposeMicroblock(mcb);
    ProposeTransition(tcb);
    _epoch_cb = ecb;
}

void
EventProposer::ProposeMicroblockOnce(MicroCb cb, std::chrono::seconds lapse)
{
    _alarm.add(std::chrono::steady_clock::now() + lapse, [this, cb]()mutable->void{
        cb();
    });
}

void
EventProposer::ProposeTransitionOnce(TransitionCb cb, std::chrono::seconds lapse)
{
    _alarm.add(std::chrono::steady_clock::now() + lapse, [this, cb]()mutable->void{
        cb();
    });
}

void
EventProposer::ProposeMicroblock(MicroCb cb)
{
    EpochTimeUtil util;
    static bool skip = true;

    std::chrono::seconds lapse = util.GetNextMicroBlockTime(skip);
    skip = false;
    _alarm.add(std::chrono::steady_clock::now() + lapse, [this, cb]()mutable->void{
        cb();
        ProposeMicroblock(cb);
    });
}

void
EventProposer::ProposeTransition(TransitionCb cb)
{
    EpochTimeUtil util;
    static bool skip = true;

    std::chrono::seconds lapse = util.GetNextEpochTime(skip);
    skip = false;
    _alarm.add(std::chrono::steady_clock::now() + lapse, [this, cb]()mutable->void{
        cb();
        ProposeTransition(cb);
    });
}

void
EventProposer::ProposeEpoch()
{
    // MicroBlocks are committed to the database in PostCommit.
    // There is some latency in PostCommit propagation to Delegates.
    // Consequently if an Epoch block is proposed too soon
    // then some Delegates might not have the most recent MicroBlock committed
    // yet, which results in bootstraping.
    // Introduce some delay (temp) to Epoch block proposal to alleviate this issue.
    // Another option (TBD) is to see if there is Commit message with the
    // most recent microblock hash and don't fail Epoch block validation.
    //_alarm.service.post(_epoch_cb);
    std::chrono::seconds lapse(5);
    _alarm.add(std::chrono::steady_clock::now() + lapse, _epoch_cb);
}
