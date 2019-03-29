/// @file
/// This file contains specialization of the ConsensusManager class, which
/// handles specifics of Epoch consensus
///
#include <logos/consensus/epoch/epoch_backup_delegate.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/lib/trace.hpp>

EpochConsensusManager::EpochConsensusManager(
                          Service & service,
                          Store & store,
                          const Config & config,
                          MessageValidator & validator,
                          p2p_interface & p2p,
                          uint32_t epoch_number)
    : Manager(service, store, config,
              validator, p2p, epoch_number)
{
	Tip tip;
    if (_store.epoch_tip_get(tip))
    {
        LOG_FATAL(_log) << "Failed to get epoch's previous hash";
        trace_and_halt();
    }
    _prev_pre_prepare_hash = tip.digest;
}

void 
EpochConsensusManager::OnBenchmarkDelegateMessage(
    std::shared_ptr<DelegateMessage> message,
    logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cur_epoch = static_pointer_cast<PrePrepare>(message);
    LOG_DEBUG (_log) << "EpochConsensusManager::OnBenchmarkDelegateMessage() - hash: "
                     << message->Hash().to_string();
}

bool 
EpochConsensusManager::Validate(
    std::shared_ptr<DelegateMessage> block,
    logos::process_return & result)
{
    result.code = logos::process_result::progress;

    return true;
}

void 
EpochConsensusManager::QueueMessagePrimary(
    std::shared_ptr<DelegateMessage> message)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto hash = message->Hash();
    // See microblock_consensus_mamanger comment for the same method
    if (_store.epoch_exists(hash) || (_cur_epoch && _cur_epoch->Hash() == hash))
    {
        return;
    }
    else if (_ongoing)
    {
        LOG_ERROR(_log) << "MicroBlockConsensusManager::QueueMessagePrimary - Unexpected scenario:"
                        << " new block (possibly from secondary list) with hash " << hash.to_string()
                        << " got promoted while current consensus round with hash " << _cur_epoch->Hash().to_string()
                        << " is still ongoing!";
        return;
    }
    _cur_epoch = static_pointer_cast<PrePrepare>(message);
}

auto
EpochConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    assert(_cur_epoch);
    _cur_epoch->timestamp = GetStamp();
    return *_cur_epoch;
}
auto
EpochConsensusManager::PrePrepareGetCurr() -> PrePrepare &
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    assert(_cur_epoch);
    return *_cur_epoch;
}

void 
EpochConsensusManager::PrePreparePopFront()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cur_epoch = nullptr;
}

bool 
EpochConsensusManager::PrePrepareQueueEmpty()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _cur_epoch == nullptr;
}

void 
EpochConsensusManager::ApplyUpdates(
    const ApprovedEB & block,
    uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block);
}

uint64_t 
EpochConsensusManager::GetStoredCount()
{
  return 1;
}

bool
EpochConsensusManager::PrimaryContains(const BlockHash &hash)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return (_cur_epoch && _cur_epoch->Hash() == hash);
}

void
EpochConsensusManager::QueueMessageSecondary(std::shared_ptr<DelegateMessage> message)
{
    uint timeout_sec = (_delegate_id + 1) * SECONDARY_LIST_TIMEOUT.count();
    if (timeout_sec > TConvert<::Seconds>(SECONDARY_LIST_TIMEOUT_CAP).count())
    {
        timeout_sec = TConvert<::Seconds>(SECONDARY_LIST_TIMEOUT_CAP).count();
    }
    _waiting_list.OnMessage(message, boost::posix_time::seconds(timeout_sec));
}

std::shared_ptr<BackupDelegate<ConsensusType::Epoch>>
EpochConsensusManager::MakeBackupDelegate(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    auto notifier = _events_notifier.lock();
    assert(notifier);
    return std::make_shared<EpochBackupDelegate>(iochannel, shared_from_this(), *this,
            _validator, ids, notifier, _persistence_manager,
            GetP2p(), _service);
}

uint8_t
EpochConsensusManager::DesignatedDelegate(
    std::shared_ptr<DelegateMessage> message)
{
	Tip tip;
    BlockHash &hash = tip.digest;
    ApprovedMB block;

    if (_store.micro_block_tip_get(tip))
    {
        LOG_FATAL(_log) << "EpochConsensusManager::DesignatedDelegate failed to get microblock tip";
        trace_and_halt();
    }

    if (_store.micro_block_get(hash, block))
    {
        LOG_FATAL(_log) << "EpochConsensusManager::DesignatedDelegate failed to get microblock";
        trace_and_halt();
    }

    // delegate who proposed last microblock also proposes epoch block
    if (block.last_micro_block && block.primary_delegate == _delegate_id)
    {
        LOG_DEBUG(_log) << "EpochConsensusManager::DesignatedDelegate epoch proposed by delegate "
                        << (int)_delegate_id << " " << (int)DelegateIdentityManager::_global_delegate_idx
                        << " " << _epoch_number
                        << " " << (int)block.primary_delegate;
        return _delegate_id;
    }

    return 0xff;
}

void
EpochConsensusManager::OnPostCommit(const PrePrepare &block)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_cur_epoch && block.Hash() == _cur_epoch->Hash())
    {
        PrePreparePopFront();
    }

    Manager::OnPostCommit(block);
}

bool
EpochConsensusManager::ProceedWithRePropose()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return !PrePrepareQueueEmpty() && Manager::ProceedWithRePropose();
}

void
EpochConsensusManager::OnConsensusReached()
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

        OnMessageQueued();
    }
}
