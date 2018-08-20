#include <logos/consensus/microblock/microblock_consensus_manager.hpp>

void 
MicroBlockConsensusManager::OnBenchmarkSendRequest(
    std::shared_ptr<RequestMessage<ConsensusType::MicroBlock>> block, 
    logos::process_return & result)
{
    _cur_microblock = static_pointer_cast<PrePrepareMessage<ConsensusType::MicroBlock>>(block);
    BOOST_LOG (_log) << "MicroBlockConsensusManager::OnBenchmarkSendRequest() - hash: " 
                     << block->hash().to_string();
}

bool 
MicroBlockConsensusManager::Validate(
    std::shared_ptr<RequestMessage<ConsensusType::MicroBlock>> block, 
    logos::process_return & result)
{
    result.code = logos::process_result::progress;
    return true;
}

void 
MicroBlockConsensusManager::QueueRequest(
    std::shared_ptr<RequestMessage<ConsensusType::MicroBlock>> request)
{
    _cur_microblock = static_pointer_cast<PrePrepareMessage<ConsensusType::MicroBlock>>(request);
}

PrePrepareMessage<ConsensusType::MicroBlock> & 
MicroBlockConsensusManager::PrePrepareGetNext()
{
  return *_cur_microblock;
}

void 
MicroBlockConsensusManager::PrePreparePopFront()
{
}

bool 
MicroBlockConsensusManager::PrePrepareQueueEmpty()
{
    return false;
}

bool 
MicroBlockConsensusManager::PrePrepareQueueFull()
{
    return false;
}

void 
MicroBlockConsensusManager::ApplyUpdates(
    const PrePrepareMessage<ConsensusType::MicroBlock> & pre_prepare, 
    uint8_t delegate_id)
{
}

uint64_t 
MicroBlockConsensusManager::OnConsensusReachedStoredCount()
{
  return 1;
}

bool 
MicroBlockConsensusManager::OnConsensusReachedExt()
{
  return false;
}