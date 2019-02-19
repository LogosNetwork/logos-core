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
                          EpochEventsNotifier & events_notifier,
                          p2p_interface & p2p)
	: Manager(service, store, config,
		      validator, events_notifier, p2p)
	, _enqueued(false)
{
	if (_store.epoch_tip_get(_prev_pre_prepare_hash))
	{
		LOG_FATAL(_log) << "Failed to get epoch's previous hash";
		trace_and_halt();
	}
}

void 
EpochConsensusManager::OnBenchmarkSendRequest(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    _cur_epoch = static_pointer_cast<PrePrepare>(block);
    LOG_DEBUG (_log) << "EpochConsensusManager::OnBenchmarkSendRequest() - hash: "
                     << block->Hash().to_string();
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
auto
EpochConsensusManager::PrePrepareGetCurr() -> PrePrepare &
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
    return (_cur_epoch && _cur_epoch->Hash() == hash);
}

void
EpochConsensusManager::QueueRequestSecondary(std::shared_ptr<Request> request)
{
    uint timeout_sec = (_delegate_id + 1) * SECONDARY_LIST_TIMEOUT.count();
    if (timeout_sec > TConvert<::Seconds>(SECONDARY_LIST_TIMEOUT_CAP).count())
    {
        timeout_sec = TConvert<::Seconds>(SECONDARY_LIST_TIMEOUT_CAP).count();
    }
    _secondary_handler.OnRequest(request, boost::posix_time::seconds(timeout_sec));
}

std::shared_ptr<BackupDelegate<ConsensusType::Epoch>>
EpochConsensusManager::MakeBackupDelegate(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    return std::make_shared<EpochBackupDelegate>(iochannel, *this, *this,
            _validator, ids, _events_notifier, _persistence_manager,
            GetP2p(), _service);
}

uint8_t
EpochConsensusManager::DesignatedDelegate(
    std::shared_ptr<Request> request)
{
    BlockHash hash;
    ApprovedMB block;

    if (_store.micro_block_tip_get(hash))
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
                        << " " << _events_notifier.GetEpochNumber()
                        << " " << (int)block.primary_delegate;
        return _delegate_id;
    }

    return 0xff;
}
