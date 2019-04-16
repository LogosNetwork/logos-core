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
                             std::shared_ptr<PrimaryDelegate> primary,
                             MessagePromoter<ECT> & promoter,
                             MessageValidator & validator,
                             const DelegateIdentities & ids,
                             ConsensusScheduler & scheduler,
                             std::shared_ptr<EpochEventsNotifier> events_notifier,
                             PersistenceManager<ECT> & persistence_manager,
                             p2p_interface & p2p,
                             Service &service)
    : BackupDelegate<ECT>(iochannel, primary, promoter, validator,
                                                ids, scheduler, events_notifier, persistence_manager, p2p, service)
    , _handler(EpochMessageHandler::GetMessageHandler())
{
    if (promoter.GetStore().epoch_tip_get(_prev_pre_prepare_hash))
    {
        LOG_FATAL(_log) << "Failed to get epoch's previous hash";
        trace_and_halt();
    }
    ApprovedEB eb;
    if (promoter.GetStore().epoch_get(_prev_pre_prepare_hash, eb))
    {
        LOG_FATAL(_log) << "EpochBackupDelegate::EpochBackupDelegate - Failed to get epoch";
        trace_and_halt();
    }
    _sequence_number = eb.sequence;
    _expected_epoch_number = eb.epoch_number + 1;
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
