//
// Created by gregt on 9/10/18.
//

#include <logos/epoch/event_proposer.hpp>
#include <logos/node/node.hpp>

EventProposer::EventProposer(logos::alarm & alarm,
                             IRecallHandler & recall_handler,
                             bool first_epoch,
                             bool first_microblock)
    : _alarm(alarm)
    , _skip_transition(first_epoch)
    , _skip_micro_block(first_microblock)
    , _recall_handler(recall_handler)
    {}

void
EventProposer::Start(
    MicroCb mcb,
    TransitionCb tcb,
    EpochCb ecb)
{
    ProposeMicroBlock(mcb);
    ProposeTransition(tcb);
    _epoch_cb = ecb;
}

void
EventProposer::ProposeMicroBlockOnce(MicroCb cb, std::chrono::seconds lapse)
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
EventProposer::ProposeMicroBlock(MicroCb cb)
{
    EpochTimeUtil util;

    // on first microblock skip 2 full intervals
    auto lapse = util.GetNextMicroBlockTime(_skip_micro_block?FIRST_MICROBLOCK_SKIP:0);
    _skip_micro_block = false;
    _alarm.add(std::chrono::steady_clock::now() + lapse, [this, cb]()mutable->void{
        cb();
        ProposeMicroBlock(cb);
    });
}

void
EventProposer::ProposeTransition(TransitionCb cb, bool next)
{
    EpochTimeUtil util;

    auto lapse = util.GetNextEpochTime(_skip_transition || _recall_handler.IsRecall());
    if (next && lapse <= EPOCH_DELEGATES_CONNECT)
    {
        lapse += EPOCH_PROPOSAL_TIME;
    }
    lapse = (lapse > EPOCH_DELEGATES_CONNECT) ? lapse - EPOCH_DELEGATES_CONNECT : lapse;
    _skip_transition = false;
    _recall_handler.Reset();
    _alarm.add(std::chrono::steady_clock::now() + lapse, [this, cb]()mutable->void{
        cb();
        ProposeTransition(cb, true);
    });
}

void
EventProposer::ProposeEpoch()
{
    _alarm.add(std::chrono::steady_clock::now(), _epoch_cb);
}
