/// @file
/// This file contains specialization of the ConsensusManager class, which
/// handles specifics of Epoch consensus
///
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>

void 
EpochConsensusManager::OnBenchmarkSendRequest(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    _cur_epoch = static_pointer_cast<PrePrepare>(block);
    BOOST_LOG (_log) << "EpochConsensusManager::OnBenchmarkSendRequest() - hash: " 
                     << block->hash().to_string();
}

bool 
EpochConsensusManager::Validate(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    result.code = logos::process_result::progress;
    return true;
}

void 
EpochConsensusManager::QueueRequest(
    std::shared_ptr<Request> request)
{
    _cur_epoch = static_pointer_cast<PrePrepare>(request);
}

auto
EpochConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
  return *_cur_epoch;
}

void 
EpochConsensusManager::PrePreparePopFront()
{
    queue = 0;
}

bool 
EpochConsensusManager::PrePrepareQueueEmpty()
{
    return !queue;
}

bool 
EpochConsensusManager::PrePrepareQueueFull()
{
    return queue;
}

void 
EpochConsensusManager::ApplyUpdates(
    const PrePrepare & pre_prepare,
    uint8_t delegate_id)
{
}

uint64_t 
EpochConsensusManager::OnConsensusReachedStoredCount()
{
  return 1;
}

bool 
EpochConsensusManager::OnConsensusReachedExt()
{
  return true;
}