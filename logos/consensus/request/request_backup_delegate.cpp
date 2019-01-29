/// @file
/// This file contains specializations of the BackupDelegate class, which
/// handle the specifics of BatchBlock consensus.
#include <logos/consensus/request/request_backup_delegate.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/lib/epoch_time_util.hpp>

#include <random>

RequestBackupDelegate::RequestBackupDelegate(
        std::shared_ptr<IOChannel> iochannel,
        PrimaryDelegate & primary,
        Promoter & promoter,
        MessageValidator & validator,
        const DelegateIdentities & ids,
        Service & service,
        EpochEventsNotifier & events_notifier,
        PersistenceManager<R> & persistence_manager,
        p2p_interface & p2p)
    : Connection(iochannel, primary, promoter,
		 validator, ids, events_notifier, persistence_manager, p2p)
    , _timer(service)
{
    ApprovedBSB block;
    promoter.GetStore().batch_tip_get(_delegate_ids.remote, _prev_pre_prepare_hash);
    if ( ! _prev_pre_prepare_hash.is_zero() && !promoter.GetStore().batch_block_get(_prev_pre_prepare_hash, block))
    {
        _sequence_number = block.sequence + 1;
    }
}

/// Validate BatchStateBlock message.
///
///     @param message message to validate
///     @return true if validated false otherwise
bool
RequestBackupDelegate::DoValidate(
    const PrePrepare & message)
{
    if(!ValidateSequence(message))
    {
        LOG_DEBUG(_log) << "RequestBackupDelegate::DoValidate ValidateSequence failed";
        return false;
    }

    // TODO: Validate if primary_delegate is correct

    if(!ValidateRequests(message))
    {
        LOG_DEBUG(_log) << "RequestBackupDelegate::DoValidate ValidateRequests failed";
        return false;
    }

    return true;
}

bool
RequestBackupDelegate::ValidateSequence(
    const PrePrepare & message)
{
    if(_sequence_number != message.sequence)
    {
        _reason = RejectionReason::Wrong_Sequence_Number;
        return false;
    }

    return true;
}

bool
RequestBackupDelegate::ValidateRequests(
    const PrePrepare & message)
{

    _rejection_map.resize(message.requests.size(), false);
    if (!_persistence_manager.ValidateBatch(message, _rejection_map))
    {
        _reason = RejectionReason::Contains_Invalid_Request;
        return false;
    }
    return true;
}

/// Commit the block to the database.
///
///     @param block to commit to the database
///     @param remote delegate id
void
RequestBackupDelegate::ApplyUpdates(
    const ApprovedBSB & block,
    uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block, delegate_id);
}

bool
RequestBackupDelegate::IsPrePrepared(
    const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_pre_prepare)
    {
        return false;
    }

    return _pre_prepare_hashes.find(hash) !=
            _pre_prepare_hashes.end();
}

void
RequestBackupDelegate::DoUpdateMessage(Rejection & message)
{
    message.reason = _reason;
    message.rejection_map = _rejection_map;
}

void
RequestBackupDelegate::Reject()
{
    switch(_reason)
    {
    case RejectionReason::Void:
        break;
    case RejectionReason::Clock_Drift:
    case RejectionReason::Contains_Invalid_Request:
    case RejectionReason::Bad_Signature:
    case RejectionReason::Invalid_Previous_Hash:
    case RejectionReason::Wrong_Sequence_Number:
    case RejectionReason::Invalid_Epoch:
    case RejectionReason::New_Epoch:
    case RejectionReason::Invalid_Primary_Index:
        Rejection msg(_pre_prepare_hash);
        DoUpdateMessage(msg);
        _validator.Sign(msg.Hash(), msg.signature);
        SendMessage<Rejection>(msg);
        break;
    }
}

void
RequestBackupDelegate::HandleReject(const PrePrepare & message)
{
    switch(_reason)
    {
        case RejectionReason::Void:
        case RejectionReason::Clock_Drift:
        case RejectionReason::Contains_Invalid_Request:
        case RejectionReason::Bad_Signature:
        case RejectionReason::Invalid_Previous_Hash:
        case RejectionReason::Wrong_Sequence_Number:
        case RejectionReason::Invalid_Epoch:
        case RejectionReason::Invalid_Primary_Index:
            break;
        case RejectionReason::New_Epoch:
            if (_events_notifier.GetDelegate() == EpochTransitionDelegate::PersistentReject)
            {
                SetPrePrepare(message);
                ScheduleTimer(GetTimeout(TIMEOUT_MIN_EPOCH, TIMEOUT_RANGE_EPOCH));
            }
            break;
    }
}

// XXX - If a Primary delegate re-proposes a subset of transactions
//       and then fails to post commit the re-proposed batch, when
//       a backup initiates fallback consensus, it is possible that
//       a transaction omitted from the re-proposed batch is forgotten,
//       since individual requests are not stored for fallback consensus.
//
// XXX - Also note: PrePrepare messages stored by backups are not
//       actually added to the secondary waiting list. Instead, they
//       stay with the backup (BackupDelegate) and are only
//       transferred when fallback consensus is to take place, in
//       which case they are transferred to the primary list
//       (RequestHandler).
void
RequestBackupDelegate::HandlePrePrepare(const PrePrepare & message)
{
    std::lock_guard<std::mutex> lock(_mutex);  // SYL Integration fix
    _pre_prepare_hashes.clear();

    for(uint64_t i = 0; i < message.requests.size(); ++i)
    {
        _pre_prepare_hashes.insert(message.requests[i]->GetHash());
    }

    // to make sure during epoch transition, a fallback session of the new epoch
    // is not rerun by the old epoch, the min timeout should be > clock_drift (i.e. 20seconds)
    ScheduleTimer(GetTimeout(TIMEOUT_MIN, TIMEOUT_RANGE));
}

void
RequestBackupDelegate::ScheduleTimer(Seconds timeout)
{
    std::lock_guard<std::mutex> lock(_timer_mutex);

    // The below condition is true when the timeout callback
    // has been scheduled and is about to be invoked. In this
    // case, the callback cannot be cancelled, and we have to
    // 'manually' cancel the callback by setting _cancel_timer.
    // When the callback is invoked, it will check this value
    // and return early.
    if(!_timer.expires_from_now(timeout) && _callback_scheduled)
    {
        _cancel_timer = true;
    }

    _timer.async_wait(
        [this](const Error & error)
        {
            OnPrePrepareTimeout(error);
        });

    _callback_scheduled = true;
}

void
RequestBackupDelegate::OnPostCommit()
{
    {
        std::lock_guard<std::mutex> lock(_timer_mutex);

        if(!_timer.cancel() && _callback_scheduled)
        {
            _cancel_timer = true;
            return;
        }

        _callback_scheduled = false;
    }

    Connection::OnPostCommit();
}

void
RequestBackupDelegate::OnPrePrepareTimeout(const Error & error)
{
    std::lock_guard<std::mutex> lock(_timer_mutex);

    if(_cancel_timer)
    {
        _cancel_timer = false;
        return;
    }

    if(error == boost::asio::error::operation_aborted)
    {
        return;
    }

    _promoter.AcquirePrePrepare(*_pre_prepare);

    _callback_scheduled = false;
}

void
RequestBackupDelegate::ResetRejectionStatus()
{
    _reason = RejectionReason::Void;
    _rejection_map.clear();
}

bool
RequestBackupDelegate::IsSubset(const PrePrepare & message)
{
    for(uint64_t i = 0; i < message.requests.size(); ++i)
    {
        if(_pre_prepare_hashes.find(message.requests[i]->GetHash()) ==
                _pre_prepare_hashes.end())
        {
            return false;
        }
    }

    return true;
}

bool
RequestBackupDelegate::ValidateReProposal(const PrePrepare & message)
{
    return IsSubset(message);
}

RequestBackupDelegate::Seconds
RequestBackupDelegate::GetTimeout(uint8_t min, uint8_t range)
{
    uint64_t offset = 0;
    uint64_t x = std::rand() % NUM_DELEGATES;

    if (x >= 2 && x < 4)
    {
        offset = range/2;
    }
    else
    {
        offset = range;
    }

    return Seconds(min + offset);
}

void
RequestBackupDelegate::CleanUp()
{
    std::lock_guard<std::mutex> lock(_timer_mutex);

    _timer.cancel();
    _cancel_timer = true;
}


