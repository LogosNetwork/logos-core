///
/// @file
/// This file contains definition of the MicroBlockBackupDelegate class
/// which handles specifics of MicroBlock consensus
///
#include <logos/consensus/microblock/microblock_backup_delegate.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/lib/trace.hpp>

MicroBlockBackupDelegate::MicroBlockBackupDelegate(
                                  std::shared_ptr<IOChannel> iochannel,
                                  std::shared_ptr<PrimaryDelegate> primary,
                                  MessagePromoter<MBCT> & promoter,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids,
                                  ArchiverMicroBlockHandler & handler,
                                  std::shared_ptr<EpochEventsNotifier> events_notifier,
                                  PersistenceManager<MBCT> & persistence_manager,
                                  p2p_interface & p2p,
                                  Service & service)
    : BackupDelegate<MBCT>(iochannel, primary, promoter, validator, ids,
                                                     events_notifier, persistence_manager, p2p, service)
    , _microblock_handler(handler)
{
    if (promoter.GetStore().micro_block_tip_get(_prev_pre_prepare_hash))
    {
        LOG_FATAL(_log) << "Failed to get microblock's previous hash";
        trace_and_halt();
    }
}

bool
MicroBlockBackupDelegate::DoValidate(
    const PrePrepare & message)
{
    return _persistence_manager.Validate(message);
}

void
MicroBlockBackupDelegate::ApplyUpdates(
    const ApprovedMB & block,
    uint8_t)
{
    _persistence_manager.ApplyUpdates(block);

    _microblock_handler.OnApplyUpdates(block);
}

bool
MicroBlockBackupDelegate::IsPrePrepared(
    const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return (_pre_prepare && hash == _pre_prepare->Hash());
}

bool
MicroBlockBackupDelegate::ValidateTimestamp(
    const PrePrepare &message )
{
    auto now = GetStamp();
    auto ts = message.timestamp;

    auto drift = now > ts ? now - ts : ts - now;

    // secondary can propose after 20*i seconds
    if(drift > TConvert<Milliseconds>(CLOCK_DRIFT).count() * (message.primary_delegate + 1))
    {
        return false;
    }

    return true;
}

