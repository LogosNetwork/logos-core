#include <logos/consensus/microblock/microblock_consensus_manager.hpp>

void
MicroBlockConsensusManager::OnBenchmarkSendRequest(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(block);
    BOOST_LOG (_log) << "MicroBlockConsensusManager::OnBenchmarkSendRequest() - hash: " 
                     << block->hash().to_string();
}

bool
MicroBlockConsensusManager::Validate(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    result.code = logos::process_result::progress;
    return true;
}

void
MicroBlockConsensusManager::QueueRequest(
    std::shared_ptr<Request> request)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(request);
}

auto
MicroBlockConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    return *_cur_microblock;
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
    const PrePrepare & pre_prepare,
    uint8_t delegate_id)
{}

uint64_t
MicroBlockConsensusManager::GetStoredCount()
{
    return 1;
}