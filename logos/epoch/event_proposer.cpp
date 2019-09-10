//
// Created by gregt on 9/10/18.
//

#include <logos/epoch/event_proposer.hpp>
#include <logos/node/node.hpp>

EventProposer::EventProposer(logos::alarm & alarm,
                             IRecallHandler & recall_handler)
    : _alarm(alarm)
    , _skip_transition(false)
    , _recall_handler(recall_handler)
    , _mb_handle(CANCELLED)
{}

void
EventProposer::Start(
    TransitionCb tcb,
    bool first_epoch)
{
    _skip_transition = first_epoch;
    ProposeTransition(tcb);
}

void
EventProposer::StartArchival(
    MicroCb mcb,
    EpochCb ecb,
    bool first_microblock)
{
    ProposeMicroBlock(mcb, first_microblock);
    _epoch_cb = ecb;
}

void
EventProposer::StopArchival()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_mb_handle != CANCELLED)
    {
        _alarm.cancel(_mb_handle);
        _mb_handle = CANCELLED;  // for the case where the alarm has been triggered but the callback hasn't been executed.
    }
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
EventProposer::ProposeMicroBlock(MicroCb cb, bool skip_micro_block)
{
    // on first microblock skip 2 full intervals
    auto lapse = ArchivalTimer::GetNextMicroBlockTime(skip_micro_block ? FIRST_MICROBLOCK_SKIP : 0);
    _mb_handle = _alarm.add(std::chrono::steady_clock::now() + lapse, [this, cb]()mutable->void{
        std::lock_guard<std::mutex> lock(_mutex);
        if (_mb_handle == CANCELLED)
        {
            return;
        }
        cb();
        ProposeMicroBlock(cb);
    });
}

void
EventProposer::ProposeTransition(TransitionCb cb, bool next)
{
    // TODO: handle the case where the EB proposal is delayed up until scheduled Epoch Transition time;
    //  also add fixed genesis start time for PTN

    // if at genesis launch or recall, skip one full epoch
    auto lapse = ArchivalTimer::GetNextEpochTime(_skip_transition || _recall_handler.IsRecall());

    if (next)
    {
        // If not just launched, alarm time must be past EPOCH_DELEGATES_CONNECT before epoch start unless at recall
        assert (lapse <= EPOCH_DELEGATES_CONNECT || _recall_handler.IsRecall());
        lapse += EPOCH_PROPOSAL_TIME;  // add time for next epoch
    }

    // The only case where lapse < connect time now is if we
    // 1) just launched, 2) are not at genesis, and 3) are past ETES time,
    // which should trigger transition immediately since we are already late.
    lapse = (lapse > EPOCH_DELEGATES_CONNECT) ? lapse - EPOCH_DELEGATES_CONNECT : Milliseconds(0);

    _skip_transition = false;
    _recall_handler.Reset();

    Log log;
    LOG_DEBUG(log) << "EventProposer::" << __func__ << " - Next transition scheduled at " << lapse.count() << "ms from now.";
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