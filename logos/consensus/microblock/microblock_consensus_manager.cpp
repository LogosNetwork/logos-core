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
                               EpochEventsNotifier & events_notifier,
                               p2p_interface & p2p)
    : Manager(service, store, config,
	      validator, events_notifier, p2p)
    , _microblock_handler(handler)
    , _enqueued(false)
{
    if (_store.micro_block_tip_get(_prev_pre_prepare_hash))
    {
        LOG_FATAL(_log) << "Failed to get microblock's previous hash";
        trace_and_halt();
    }
}

void
MicroBlockConsensusManager::OnBenchmarkDelegateMessage(
    std::shared_ptr<DelegateMessage> message,
    logos::process_return & result)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(message);
    LOG_DEBUG (_log) << "MicroBlockConsensusManager::OnBenchmarkDelegateMessage() - hash: "
                     << message->Hash().to_string();
}

bool
MicroBlockConsensusManager::Validate(
    std::shared_ptr<DelegateMessage> message,
    logos::process_return & result)
{
    result.code = logos::process_result::progress;

    return true;
}

void
MicroBlockConsensusManager::QueueMessagePrimary(
    std::shared_ptr<DelegateMessage> message)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(message);
    _enqueued = true;
}

auto
MicroBlockConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    return *_cur_microblock;
}

auto
MicroBlockConsensusManager::PrePrepareGetCurr() -> PrePrepare &
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
    return (_cur_microblock && _cur_microblock->Hash() == hash);
}

void
MicroBlockConsensusManager::QueueMessageSecondary(std::shared_ptr<DelegateMessage> message)
{
    _waiting_list.OnRequest(message,
        boost::posix_time::seconds(_delegate_id * SECONDARY_LIST_TIMEOUT.count()));
}

std::shared_ptr<BackupDelegate<ConsensusType::MicroBlock>>
MicroBlockConsensusManager::MakeBackupDelegate(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    return std::make_shared<MicroBlockBackupDelegate>(iochannel, *this, *this,
            _validator, ids, _microblock_handler, _events_notifier, _persistence_manager, Manager::_consensus_p2p._p2p);
}
