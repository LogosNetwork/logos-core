///
/// @file
/// This file contains declaration of the EpochBackupDelegate class
/// which handles specifics of Epoch consensus
///
#include <logos/consensus/epoch/epoch_backup_delegate.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/lib/trace.hpp>

EpochBackupDelegate::EpochBackupDelegate(
                             std::shared_ptr<IOChannel> iochannel,
                             PrimaryDelegate & primary,
                             RequestPromoter<ECT> & promoter,
                             MessageValidator & validator,
                             const DelegateIdentities & ids,
                             EpochEventsNotifier & events_notifier,
                             PersistenceManager<ECT> & persistence_manager)
    : BackupDelegate<ECT>(iochannel, primary, promoter, validator,
                                                ids, events_notifier, persistence_manager)
{
    if (promoter.GetStore().epoch_tip_get(_prev_pre_prepare_hash))
    {
        LOG_FATAL(_log) << "Failed to get eporh's previous hash";
        trace_and_halt();
    }
}

bool
EpochBackupDelegate::DoValidate(
    const PrePrepare & message)
{
    return _persistence_manager.Validate(message);
}

void
EpochBackupDelegate::ApplyUpdates(
    const ApprovedEB & block,
    uint8_t)
{
    _persistence_manager.ApplyUpdates(block);
}

bool
EpochBackupDelegate::IsPrePrepared(
    const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return (_pre_prepare && hash == _pre_prepare->Hash());
}
