#include <logos/consensus/microblock/microblock_consensus_manager.hpp>

void MicroBlockConsensusManager::OnBenchmarkSendRequest(std::shared_ptr<RequestMessage<ConsensusType::MicroBlock>> block, logos::process_return & result)
{
    _cur_microblock = static_pointer_cast<PrePrepareMessage<ConsensusType::MicroBlock>>(block);
    BOOST_LOG (_log) << "MicroBlockConsensusManager::OnBenchmarkSendRequest() - hash: " << block->hash().to_string();
}

bool MicroBlockConsensusManager::Validate(std::shared_ptr<RequestMessage<ConsensusType::MicroBlock>> block, logos::process_return & result)
{
    result.code = logos::process_result::progress;
    return true;
}

void MicroBlockConsensusManager::QueueRequest(std::shared_ptr<RequestMessage<ConsensusType::MicroBlock>> request)
{
    _cur_microblock = static_pointer_cast<PrePrepareMessage<ConsensusType::MicroBlock>>(request);
}

PrePrepareMessage<ConsensusType::MicroBlock> & MicroBlockConsensusManager::PrePrepare_GetNext()
{
  return *_cur_microblock;
}

void MicroBlockConsensusManager::PrePrepare_PopFront()
{
}

bool MicroBlockConsensusManager::PrePrepare_QueueEmpty()
{
    return false;
}

bool MicroBlockConsensusManager::PrePrepare_QueueFull()
{
    return false;
}

void MicroBlockConsensusManager::ApplyUpdates(const PrePrepareMessage<ConsensusType::MicroBlock> & pre_prepare, uint8_t delegate_id)
{
}

uint64_t MicroBlockConsensusManager::OnConsensusReached_StoredCount()
{
  return 1;
}

bool MicroBlockConsensusManager::OnConsensusReached_Ext()
{
  return false;
}