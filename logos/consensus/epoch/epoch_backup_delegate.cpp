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
                             std::shared_ptr<EpochEventsNotifier> events_notifier,
                             PersistenceManager<ECT> & persistence_manager,
                             p2p_interface & p2p,
                             Service &service)
    : BackupDelegate<ECT>(iochannel, primary, promoter, validator,
                                                ids, events_notifier, persistence_manager, p2p, service)
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


bool
EpochBackupDelegate::ValidateTimestamp(
    const PrePrepare &message )
{
    auto now = GetStamp();
    auto ts = message.timestamp;

    auto drift = now > ts ? now - ts : ts - now;

    // secondary can propose up to a timeout cap later
    if(drift > TConvert<Milliseconds>(SECONDARY_LIST_TIMEOUT_CAP+CLOCK_DRIFT).count())
    {
        return false;
    }

    return true;
}
