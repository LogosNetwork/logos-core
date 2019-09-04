/// @file
/// This file contains implementation of the EpochHandler class, which is used
/// in the Epoch processing
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/rewards/epoch_rewards_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/trace.hpp>
#include <logos/lib/log.hpp>

uint64_t
EpochHandler::ComputeNumRBs(BlockStore &store, uint32_t epoch_number)
{
    uint64_t total_RBs = 0;
    for(uint8_t i=0; i < NUM_DELEGATES; ++i)
    {
        Tip tip;
        store.request_tip_get(i, epoch_number, tip);
        assert(tip.epoch <= epoch_number);
        if(tip.epoch == epoch_number &&
            ! tip.digest.is_zero()) // to be safe, we also test digest
        {
            total_RBs += tip.sqn + 1;
        }
    }
    return total_RBs;
}

bool
EpochHandler::Build(DelegateMessage<ConsensusType::Epoch> &epoch)
{
    Tip epoch_tip;
    Tip micro_tip;
    BlockHash & previous_epoch_hash = epoch_tip.digest;
    BlockHash & previous_micro_block_hash = micro_tip.digest;
    ApprovedEB previous_epoch;
    ApprovedMB last_micro_block;

    if (_store.epoch_tip_get(epoch_tip))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get epoch: "
                        << previous_epoch_hash.to_string();
        trace_and_halt();
    }

    if (_store.micro_block_tip_get(micro_tip))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get micro block tip";
        trace_and_halt();
    }

    if (_store.micro_block_get(previous_micro_block_hash, last_micro_block))
    {
        LOG_FATAL(_log) << "EpochHandler::Build failed to get micro block: "
                        << previous_micro_block_hash.to_string();
        trace_and_halt();
    }

    const uint32_t INFLATION_RATE_FACTOR = 1000000;

    epoch.timestamp = GetStamp();
    epoch.previous = previous_epoch_hash;
    epoch.primary_delegate = 0xff;//epoch_handler does not know the delegate index which could change after every epoch transition
    epoch.epoch_number = previous_epoch.epoch_number + 1;
    epoch.micro_block_tip = micro_tip;//previous_micro_block_hash;
    //Note, we write epoch block with epoch number i at the beginning of epoch i+1
    epoch.is_extension = !_voting_manager.GetNextEpochDelegates(epoch.delegates,epoch.epoch_number+1);

    epoch.transaction_fee_pool = 0;
    if(EpochRewardsManager::GetInstance()->GetFeePool(epoch.epoch_number, epoch.transaction_fee_pool))
    {
        LOG_WARN(_log) << "EpochHandler::Build failed to get fee pool for epoch: "
                        << epoch.epoch_number;
    }

    auto total_supply = (logos::uint256_t(previous_epoch.total_supply.number()) *
                         logos::uint256_t(LOGOS_INFLATION_RATE * INFLATION_RATE_FACTOR)) / INFLATION_RATE_FACTOR;

    if(total_supply <= previous_epoch.total_supply.number())
    {
        LOG_ERROR(_log) << "EpochHandler::Build: Inflating total supply resulted in overflow.";
    }
    else
    {
        epoch.total_supply = total_supply.convert_to<logos::uint128_t>();
    }

    //TODO to be safe, all reads from DB should be under the same read transaction when building an object, EB, MB, etc.
    //Maybe Ok for now without, since building EB and MB are delayed.

    //total_RBs
    epoch.total_RBs = previous_epoch.total_RBs + ComputeNumRBs(_store, epoch.epoch_number);

    LOG_INFO(_log) << "EpochHandler::Build, built epoch block:"
                   << " hash " << epoch.Hash().to_string()
                   << " timestamp " << epoch.timestamp
                   << " previous " << epoch.previous.to_string()
                   << " epoch_number " << epoch.epoch_number
                   << " micro_block_tip " << epoch.micro_block_tip.to_string()
                   << " total_request_blocks " << epoch.total_RBs;

    return true;
}
