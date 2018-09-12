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
    TransitionCb tcb)
{
    ProposeMicroblock(mcb);
    ProposeTransition(tcb);
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
