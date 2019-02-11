/// @file
/// This file contains implementation of the EpochHandler class, which is used
/// in the Epoch processing
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/trace.hpp>
#include <logos/lib/log.hpp>

bool
EpochHandler::Build(RequestMessage<ConsensusType::Epoch> &epoch)
{
    BlockHash previous_epoch_hash;
    BlockHash previous_micro_block_hash;
    ApprovedEB previous_epoch;
    ApprovedMB last_micro_block;

    if (_store.epoch_tip_get(previous_epoch_hash))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get epoch: " <<
            previous_epoch_hash.to_string();
        trace_and_halt();
    }

    if (_store.micro_block_tip_get(previous_micro_block_hash))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get micro block tip";
        trace_and_halt();
    }

    if (_store.micro_block_get(previous_micro_block_hash, last_micro_block))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get micro block: " <<
            previous_micro_block_hash.to_string();
        trace_and_halt();
    }

    epoch.timestamp = GetStamp();
    epoch.previous = previous_epoch_hash;
    epoch.primary_delegate = 0xff;//epoch_handler does not know the delegate index which could change after every epoch transition
    epoch.epoch_number = previous_epoch.epoch_number + 1;
    epoch.micro_block_tip = previous_micro_block_hash;
    _voting_manager.GetNextEpochDelegates(epoch.delegates);
    epoch.transaction_fee_pool = 0; // TODO

    LOG_INFO(_log) << "EpochHandler::Build, built epoch block:"
                   << " hash " << epoch.Hash().to_string()
                   << " timestamp " << epoch.timestamp
                   << " previous " << epoch.previous.to_string()
                   << " epoch_number " << epoch.epoch_number
                   << " micro_block_tip " << epoch.micro_block_tip.to_string();

    return true;
}
