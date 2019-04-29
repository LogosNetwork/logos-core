#include <logos/consensus/microblock/microblock_backup_delegate.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/lib/trace.hpp>

MicroBlockConsensusManager::MicroBlockConsensusManager(
                               Service & service,
                               Store & store,
                               const Config & config,
                               ConsensusScheduler & scheduler,
                               MessageValidator & validator,
                               ArchiverMicroBlockHandler & handler,
                               p2p_interface & p2p,
                               uint32_t epoch_number)
    : Manager(service, store, config,
	      scheduler, validator, p2p, epoch_number)
    , _microblock_handler(handler)
    , _handler(MicroBlockMessageHandler::GetMessageHandler())
    , _secondary_timeout(Seconds(_delegate_id * SECONDARY_LIST_TIMEOUT.count()))
{
    Tip tip;
    if (_store.micro_block_tip_get(tip))
    {
        LOG_FATAL(_log) << "Failed to get microblock's previous hash";
        trace_and_halt();
    }
    _prev_pre_prepare_hash = tip.digest;
}

void
MicroBlockConsensusManager::OnBenchmarkDelegateMessage(
    std::shared_ptr<DelegateMessage> message,
    logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cur_microblock = static_pointer_cast<PrePrepare>(message);
    LOG_DEBUG (_log) << "MicroBlockConsensusManager::OnBenchmarkDelegateMessage() - hash: "
                     << message->Hash().to_string();
}

bool
MicroBlockConsensusManager::Validate(
    std::shared_ptr<DelegateMessage> message,
    logos::process_return & result)
{
    if (_store.micro_block_exists(message->Hash()))
    {
        result.code = logos::process_result::old;
        return false;
    }

    result.code = logos::process_result::progress;
    return true;
}

auto
MicroBlockConsensusManager::PrePrepareGetNext(bool) -> PrePrepare &
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cur_microblock = static_pointer_cast<PrePrepare>(_handler.GetFront());
    assert(_cur_microblock);
    _cur_microblock->primary_delegate = GetDelegateIndex();
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
    assert(_cur_microblock);
    _handler.OnPostCommit(_cur_microblock);
    _cur_microblock = nullptr;
}

bool
MicroBlockConsensusManager::InternalQueueEmpty()
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
MicroBlockConsensusManager::InternalContains(const BlockHash &hash)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return (_cur_microblock && _cur_microblock->Hash() == hash);
}

const MicroBlockConsensusManager::Seconds &
MicroBlockConsensusManager::GetSecondaryTimeout()
{
    return _secondary_timeout;
}

std::shared_ptr<BackupDelegate<ConsensusType::MicroBlock>>
MicroBlockConsensusManager::MakeBackupDelegate(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    auto notifier = GetSharedPtr(_events_notifier,
            "MicroBlockConsensusManager::MakeBackupDelegate, object destroyed");
    assert(notifier);
    return std::make_shared<MicroBlockBackupDelegate>(iochannel, shared_from_this(), _store,
            _validator, ids, _microblock_handler, _scheduler, notifier, _persistence_manager,
            GetP2p(), _service);
}

bool MicroBlockConsensusManager::AlreadyPostCommitted()
{
    if (!_cur_microblock) return true;
    // only reason for ConsensusManager's current block hash to not exist in main queue is backup's removal
    return !_handler.Contains(_cur_microblock->Hash());
}
