#include <logos/consensus/microblock/microblock_consensus_connection.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>

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
MicroBlockConsensusManager::QueueRequestPrimary(
    std::shared_ptr<Request> request)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(request);
    _enqueued = true;
}

auto
MicroBlockConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    return *_cur_microblock;
}

void
MicroBlockConsensusManager::PrePreparePopFront()
{
    _cur_microblock.reset();
    _enqueued = false;
}

bool
MicroBlockConsensusManager::PrePrepareQueueEmpty()
{
    return !_enqueued;
}

bool
MicroBlockConsensusManager::PrePrepareQueueFull()
{
    return _enqueued;
}

void
MicroBlockConsensusManager::ApplyUpdates(
    const PrePrepare & pre_prepare,
    uint8_t delegate_id)
{
	_microblock_handler.CommitToDatabase(pre_prepare);
}

uint64_t 
MicroBlockConsensusManager::GetStoredCount()
{
    return 1;
}

bool
MicroBlockConsensusManager::PrimaryContains(const logos::block_hash &hash)
{
    return (_cur_microblock && _cur_microblock->hash() == hash);
}

void
MicroBlockConsensusManager::QueueRequestSecondary(std::shared_ptr<Request> request)
{
    _secondary_handler.OnRequest(request,
        boost::posix_time::seconds(_delegate_id * SECONDARY_LIST_TIMEOUT.count()));
}

std::shared_ptr<ConsensusConnection<ConsensusType::MicroBlock>>
MicroBlockConsensusManager::MakeConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    return std::make_shared<MicroBlockConsensusConnection>(iochannel, *this, *this,
            _validator, ids, _microblock_handler, _events_notifier);
}
