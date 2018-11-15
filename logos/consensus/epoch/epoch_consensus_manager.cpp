/// @file
/// This file contains specialization of the ConsensusManager class, which
/// handles specifics of Epoch consensus
///
#include <logos/consensus/epoch/epoch_consensus_connection.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/lib/trace.hpp>

EpochConsensusManager::EpochConsensusManager(
                          Service & service,
	                      Store & store,
					      const Config & config,
                          DelegateKeyStore & key_store,
                          MessageValidator & validator,
                          ArchiverEpochHandler & handler,
                          EpochEventsNotifier & events_notifier)
	: Manager(service, store, config,
		      key_store, validator, events_notifier)
	, _epoch_handler(handler)
	, _enqueued(false)
{
	if (_store.epoch_tip_get(_prev_hash))
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
    uint timeout_sec = (_delegate_id + 1) * SECONDARY_LIST_TIMEOUT.count();
    if (timeout_sec > TConvert<::Seconds>(SECONDARY_LIST_TIMEOUT_CAP).count())
    {
        timeout_sec = TConvert<::Seconds>(SECONDARY_LIST_TIMEOUT_CAP).count();
    }
    _secondary_handler.OnRequest(request, boost::posix_time::seconds(timeout_sec));
}

std::shared_ptr<ConsensusConnection<ConsensusType::Epoch>>
EpochConsensusManager::MakeConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    return std::make_shared<EpochConsensusConnection>(iochannel, *this, *this,
            _validator, ids, _epoch_handler, _events_notifier);
}

uint8_t
EpochConsensusManager::DesignatedDelegate(
    std::shared_ptr<Request> request)
{
    BlockHash hash;
    MicroBlock block;

    if (_store.micro_block_tip_get(hash))
    {
        LOG_ERROR(_log) << "EpochConsensusManager::DesignatedDelegate failed to get microblock tip";
        return 0;
    }

    if (_store.micro_block_get(hash, block))
    {
        LOG_ERROR(_log) << "EpochConsensusManager::DesignatedDelegate failed to get microblock";
        return 0;
    }

    // delegate who proposed last microblock also proposes epoch block
    if (block.last_micro_block && block.account == DelegateIdentityManager::_delegate_account)
    {
        LOG_DEBUG(_log) << "EpochConsensusManager::DesignatedDelegate epoch proposed by delegate "
                        << (int)_delegate_id << " " << (int)DelegateIdentityManager::_global_delegate_idx
                        << " " << _events_notifier.GetEpochNumber()
                        << " " << block.account.to_string();
        return _delegate_id;
    }

    return 0;
}
