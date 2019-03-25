#include <logos/consensus/microblock/microblock_backup_delegate.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/lib/trace.hpp>

MicroBlockConsensusManager::MicroBlockConsensusManager(
                               Service & service,
                               Store & store,
                               const Config & config,
                               MessageValidator & validator,
                               ArchiverMicroBlockHandler & handler,
                               p2p_interface & p2p,
                               uint32_t epoch_number)
    : Manager(service, store, config,
	      validator, p2p, epoch_number)
    , _microblock_handler(handler)
{
    if (_store.micro_block_tip_get(_prev_pre_prepare_hash))
    {
        LOG_FATAL(_log) << "Failed to get microblock's previous hash";
        trace_and_halt();
    }
}

void
MicroBlockConsensusManager::OnBenchmarkSendRequest(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cur_microblock = static_pointer_cast<PrePrepare>(block);
    LOG_DEBUG (_log) << "MicroBlockConsensusManager::OnBenchmarkSendRequest() - hash: "
                     << block->Hash().to_string();
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
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto hash = request->Hash();

    // Super edge case scenario example:
    // 1) at `t` the primary proposes, doesn't complete,
    // 2) at `t+40s` some other fallback consensus reaches the original primary, doesn't complete,
    // 3a) at `t+1min` the primary's secondary list times out,
    // 3b) simultaneously its own PrePrepare timer also times out,
    // Outcome: the primary's reference value may get corrupted
    // Hence we need to skip if _cur_microblock hash is the same
    if (_store.micro_block_exists(hash) || (_cur_microblock && _cur_microblock->Hash() == hash))
    {
        return;
    }
    else if (_ongoing)
    {
        LOG_ERROR(_log) << "MicroBlockConsensusManager::QueueMessagePrimary - Unexpected scenario:"
                        << " new block (possibly from secondary list) with hash " << hash.to_string()
                        << " got promoted while current consensus round with hash " << _cur_microblock->Hash().to_string()
                        << " is still ongoing!";
        return;
    }
    _cur_microblock = static_pointer_cast<PrePrepare>(request);
}

auto
MicroBlockConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    assert(_cur_microblock);
    _cur_microblock->timestamp = GetStamp();
    return *_cur_microblock;
}

auto
MicroBlockConsensusManager::PrePrepareGetCurr() -> PrePrepare &
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    assert(_cur_microblock);
    return *_cur_microblock;
}

void
MicroBlockConsensusManager::PrePreparePopFront()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cur_microblock = nullptr;
}

bool
MicroBlockConsensusManager::PrePrepareQueueEmpty()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _cur_microblock == nullptr;
}

void
MicroBlockConsensusManager::ApplyUpdates(
    const ApprovedMB & block,
    uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block);

    _microblock_handler.OnApplyUpdates(block);
}

uint64_t 
MicroBlockConsensusManager::GetStoredCount()
{
    return 1;
}

bool
MicroBlockConsensusManager::PrimaryContains(const BlockHash &hash)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return (_cur_microblock && _cur_microblock->Hash() == hash);
}

void
MicroBlockConsensusManager::QueueRequestSecondary(std::shared_ptr<Request> request)
{
    _secondary_handler.OnRequest(request,
        boost::posix_time::seconds(_delegate_id * SECONDARY_LIST_TIMEOUT.count()));
}

std::shared_ptr<BackupDelegate<ConsensusType::MicroBlock>>
MicroBlockConsensusManager::MakeBackupDelegate(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    auto notifier = _events_notifier.lock();
    assert(notifier);
    return std::make_shared<MicroBlockBackupDelegate>(iochannel, *this, *this,
            _validator, ids, _microblock_handler, notifier, _persistence_manager,
            GetP2p(), _service);
}

void
MicroBlockConsensusManager::OnPostCommit(const PrePrepare &block)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_cur_microblock && block.Hash() == _cur_microblock->Hash())
    {
        PrePreparePopFront();
    }

    Manager::OnPostCommit(block);
}

bool
MicroBlockConsensusManager::ProceedWithRePropose()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return !PrePrepareQueueEmpty() && Manager::ProceedWithRePropose();
}

void
MicroBlockConsensusManager::OnConsensusReached()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!PrePrepareQueueEmpty())
    {
        Manager::OnConsensusReached();
    }
    // it was already committed by the backup
    else
    {
        SetPreviousPrePrepareHash(_pre_prepare_hash);

        PrePreparePopFront();

        _ongoing = false;

        OnRequestQueued();
    }
}
