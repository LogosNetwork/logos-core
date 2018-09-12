///
/// @file
/// This file contains definition of the Archiver class - container for epoch/microblock handling related classes
///

#include <logos/epoch/archiver.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/lib/epoch_time_util.hpp>

Archiver::Archiver(logos::alarm & alarm,
                   BlockStore & store,
                   uint8_t delegate_id,
                   ConsensusContainer & container)
    : _micro_block_handler(store, delegate_id)
    , _epoch_handler(store)
    , _event_proposer(alarm)
    , _consensus_container(container)
    {}

void
Archiver::Start()
{
    /*auto microcb = [this](){
        EpochTimeUtil util;
        auto micro_block = std::make_shared<MicroBlock>();
       _micro_block_handler.BuildMicroBlock(*micro_block, util.IsEpochTime());
       _consensus_container.OnSendRequest(micro_block);
    };*/

    auto microcb_dummy = [this]()->void{
        BOOST_LOG(_log) << "EventProposer::ProposeMicroblock " << "MICROBLOCK IS NOT PROPOSED";
    };

    auto epochcb = [this](){
        BOOST_LOG(_log) << "Archiver::Start " << "EPOCH BLOCK IS NOT PROPOSED";
    };

    _event_proposer.Start(microcb_dummy, epochcb);
}