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
                                  ConsensusScheduler & scheduler,
                                  std::shared_ptr<EpochEventsNotifier> events_notifier,
                                  PersistenceManager<MBCT> & persistence_manager,
                                  p2p_interface & p2p,
                                  Service & service)
    : BackupDelegate<MBCT>(iochannel, primary, promoter, validator, ids, scheduler,
                                                     events_notifier, persistence_manager, p2p, service)
    , _handler(MicroBlockMessageHandler::GetMessageHandler())
    , _microblock_handler(handler)
{
    Tip tip;
    if (promoter.GetStore().micro_block_tip_get(tip))
    {
        LOG_FATAL(_log) << "Failed to get microblock's previous hash";
        trace_and_halt();
    }
    _prev_pre_prepare_hash = tip.digest;
    ApprovedMB mb;
    if (promoter.GetStore().micro_block_get(_prev_pre_prepare_hash, mb))
    {
        LOG_FATAL(_log) << "MicroBlockBackupDelegate::MicroBlockBackupDelegate - Failed to get microblock";
        trace_and_halt();
    }
    _sequence_number = mb.sequence + 1;
    _expected_epoch_number = mb.epoch_number;
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

void
MicroBlockBackupDelegate::AdvanceCounter()
{
    if (_pre_prepare->last_micro_block)
    {
        _sequence_number = 0;
        _expected_epoch_number = _pre_prepare->epoch_number + 1;
    }
    else
    {
        _sequence_number = _pre_prepare->sequence + 1;
    }
}
