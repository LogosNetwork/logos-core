/// @file
/// This file contains specialization of the ConsensusManager class, which
/// handles specifics of Epoch consensus
///
#include <logos/consensus/epoch/epoch_consensus_connection.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>

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
EpochConsensusManager::QueueRequestPrimary(
    std::shared_ptr<Request> request)
{
    _cur_epoch = static_pointer_cast<PrePrepare>(request);
    _enqueued = true;
}

auto
EpochConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
  return *_cur_epoch;
}

void 
EpochConsensusManager::PrePreparePopFront()
{
    _cur_epoch.reset();
    _enqueued = false;
}

bool 
EpochConsensusManager::PrePrepareQueueEmpty()
{
    return !_enqueued;
}

bool 
EpochConsensusManager::PrePrepareQueueFull()
{
    return _enqueued;
}

void 
EpochConsensusManager::ApplyUpdates(
    const PrePrepare & pre_prepare,
    uint8_t delegate_id)
{
    _epoch_handler.CommitToDatabase(pre_prepare);
}

uint64_t 
EpochConsensusManager::GetStoredCount()
{
  return 1;
}

bool
EpochConsensusManager::PrimaryContains(const logos::block_hash &hash)
{
    return (_cur_epoch && _cur_epoch->hash() == hash);
}

void
EpochConsensusManager::QueueRequestSecondary(std::shared_ptr<Request> request)
{
    _secondary_handler.OnRequest(request,
        boost::posix_time::seconds(_delegate_id * SECONDARY_LIST_TIMEOUT.count()));
}

std::shared_ptr<ConsensusConnection<ConsensusType::Epoch>>
EpochConsensusManager::MakeConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    return std::make_shared<EpochConsensusConnection>(iochannel, *this, *this,
            _validator, ids, _epoch_handler, _events_notifier);
}
