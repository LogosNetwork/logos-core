///
/// @file
/// This file contains definition of EpochTimeUtil - epoch time related functions
///
#include <logos/lib/epoch_time_util.hpp>
#include <logos/node/node.hpp>
#include <logos/consensus/messages/common.hpp>

template<typename T>
Milliseconds
EpochTimeUtil::GetNextTime(T timeout, uint8_t skip)
{
    auto now = GetStamp();
    auto timeout_msec = TConvert<Milliseconds>(timeout).count();
    auto rem = now % timeout_msec;

    if (rem != 0)
    {
        return  Milliseconds(timeout_msec * (skip +1) - rem);
    }
    else if (skip != 0)
    {
        return Milliseconds(timeout_msec * skip);
    }
    else
    {
        return Milliseconds(0);
    }
}

Milliseconds
EpochTimeUtil::GetNextEpochTime(
        uint8_t skip)
{
    return GetNextTime(EPOCH_PROPOSAL_TIME, skip);
}

/// Microblock proposal happens on 10 min boundary
Milliseconds
EpochTimeUtil::GetNextMicroBlockTime(
        uint8_t skip)
{
    return GetNextTime(MICROBLOCK_PROPOSAL_TIME, skip);
}

bool
EpochTimeUtil::IsPastEpochBlockTime()
{
    auto now = GetStamp();
    auto epoch = TConvert<Milliseconds>(EPOCH_PROPOSAL_TIME).count();
    auto rem = now % epoch;
    auto min = TConvert<Milliseconds>(MICROBLOCK_PROPOSAL_TIME - CLOCK_DRIFT).count();

    return rem > min;
}

/* This function calculates the appropriate time out for reproposing archival
 * blocks. If the first proposal fails, every 20 seconds a new delegate will
 * attempt to propose the same block (this is due to secondary waiting list
 * timeout). Each delegate will wait 60 seconds, and if their proposal fails,
 * propose a second time with p2p consensus. If every delegate proposes twice
 * (once via direct, once via direct and p2p), then the network enters a semi
 * idle state, with one delegate proposing each minute, via direct and p2p.
 * If each delegate proposes once in the semi idle state and consensus is still
 * not reached, the network enters an idle state with one delegate proposing
 * every ten minutes.
 */
template <ConsensusType CT>
long EpochTimeUtil::GetTimeout(uint8_t num_proposals, uint8_t delegate_id)
{
    long base_timeout = 0;
    long base_offset = 0;
    long buffer = 0;
    assert(num_proposals != 0);
    if(num_proposals == 1)
    {
        //first proposal, simply wait PRIMARY_TIMEOUT
        //if this timeout expires, p2p consensus will be enabled
        return PRIMARY_TIMEOUT.count();
    }
    else if(num_proposals == 2)
    {
        //if second proposal fails, one delegate per minute will propose
        //find this delegate's minute
        base_timeout = (delegate_id + 1) * ARCHIVAL_TIMEOUT_SEMI_IDLE.count();
        //need to wait until all delegates have proposed once
        base_offset = (NUM_DELEGATES - delegate_id - 1) * SECONDARY_LIST_TIMEOUT.count();
        //give a 60 second buffer between when delegate 31 proposes for the first time
        buffer = PRIMARY_TIMEOUT.count();
    }
    else
    {
        //if third proposal fails, one delegate per ten minutes will propose
        base_timeout = (delegate_id + 1) * ARCHIVAL_TIMEOUT_IDLE.count();
        if(num_proposals == 3)
        {
            //wait until all delegates have proposed in the prior round
            //of 1 proposal per minute
            base_offset = (NUM_DELEGATES - delegate_id - 1) * ARCHIVAL_TIMEOUT_SEMI_IDLE.count();
        }
        else
        {
            //wait until all delegates have proposed in prior round of 1 proposal
            //every ten minutes
            base_offset = (NUM_DELEGATES - delegate_id -1) * ARCHIVAL_TIMEOUT_IDLE.count();
        
        }
    }
    return base_timeout + base_offset + buffer;
        
}

template long EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(uint8_t num_proposals, uint8_t delegate_id);

template long EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(uint8_t num_proposals, uint8_t delegate_id);

template<>
long EpochTimeUtil::GetTimeout<ConsensusType::Request>(uint8_t num_proposals, uint8_t delegate_id)
{
    assert(num_proposals != 0);
    long multiplier = (long) pow(2, num_proposals - 1);
    if(multiplier > 8)
    {
        multiplier = 10;
    }
    return PRIMARY_TIMEOUT.count() * multiplier;
}
