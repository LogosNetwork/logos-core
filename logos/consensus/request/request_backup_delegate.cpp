/// @file
/// This file contains specializations of the BackupDelegate class, which
/// handle the specifics of BatchBlock consensus.
#include <logos/consensus/request/request_backup_delegate.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/lib/epoch_time_util.hpp>

#include <random>

RequestBackupDelegate::RequestBackupDelegate(
        std::shared_ptr<IOChannel> iochannel,
        std::shared_ptr<PrimaryDelegate> primary,
        Store & store,
        MessageValidator & validator,
        const DelegateIdentities & ids,
        Service & service,
        ConsensusScheduler & scheduler,
        std::shared_ptr<EpochEventsNotifier> events_notifier,
	    PersistenceManager<R> & persistence_manager,
	    p2p_interface & p2p)
    : Connection(iochannel, primary, store,
		 validator, ids, scheduler, events_notifier, persistence_manager, p2p, service)
    , _handler(RequestMessageHandler::GetMessageHandler())
{
    ApprovedRB block;
    _expected_epoch_number = events_notifier->GetEpochNumber();
    Tip tip;
    store.request_tip_get(_delegate_ids.remote, _expected_epoch_number, tip);
    _prev_pre_prepare_hash = tip.digest;
    if ( ! _prev_pre_prepare_hash.is_zero() && !store.request_block_get(_prev_pre_prepare_hash, block))
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
    const ApprovedRB & block,
    uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block, delegate_id);
}

void
RequestBackupDelegate::DoUpdateMessage(Rejection & message)
{
    message.reason = _reason;
    message.rejection_map = _rejection_map;
}

void
RequestBackupDelegate::Reject(const BlockHash & preprepare_hash)
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
        Rejection msg(preprepare_hash);
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
        case RejectionReason::Contains_Invalid_Request:
        {
            // If reason is contain invalid request, still need to queue up requests we agree on
            std::lock_guard<std::mutex> lock(_mutex);  // SYL Integration fix
            _pre_prepare_hashes.clear();
            auto timeout = Clock::now() + GetTimeout(TIMEOUT_MIN, TIMEOUT_RANGE);
            for(uint16_t i = 0; i < message.requests.size(); ++i)
            {
                if (!_rejection_map[i])
                {
                    _handler.OnMessage(std::static_pointer_cast<Request>(message.requests[i]), timeout);
                }
            }
            _scheduler.ScheduleTimer(R, timeout);
            break;
        }
        case RejectionReason::Void:
        case RejectionReason::Clock_Drift:
        case RejectionReason::Bad_Signature:
        case RejectionReason::Invalid_Previous_Hash:
        case RejectionReason::Wrong_Sequence_Number:
        case RejectionReason::Invalid_Epoch:
        case RejectionReason::Invalid_Primary_Index:
            break;
        case RejectionReason::New_Epoch:
            auto notifier = GetSharedPtr(_events_notifier,
                    "RequestBackupDelegate::HandlerReject, object destroyed");
            if (notifier && notifier->GetDelegate() == EpochTransitionDelegate::PersistentReject)
            {
                SetPrePrepare(message);
                auto timeout = GetTimeout(TIMEOUT_MIN, TIMEOUT_RANGE);
                for(uint64_t i = 0; i < message.requests.size(); ++i)
                {
                    _handler.OnMessage(std::static_pointer_cast<Request>(message.requests[i]), timeout);
                }
                _scheduler.ScheduleTimer(R, Clock::now() + timeout);
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
//       (MessageHandler).
void
RequestBackupDelegate::HandlePrePrepare(const PrePrepare & message)
{
    // TODO: should we accept partial message?
    std::lock_guard<std::mutex> lock(_mutex);  // SYL Integration fix
    _pre_prepare_hashes.clear();

    auto timeout = Clock::now() + GetTimeout(TIMEOUT_MIN, TIMEOUT_RANGE);
    for(uint64_t i = 0; i < message.requests.size(); ++i)
    {
        _pre_prepare_hashes.insert(message.requests[i]->GetHash());
        _handler.OnMessage(std::static_pointer_cast<Request>(message.requests[i]), timeout);
    }

    // to make sure during epoch transition, a fallback session of the new epoch
    // is not rerun by the old epoch, the min timeout should be > clock_drift (i.e. 20seconds)
    _scheduler.ScheduleTimer(R, timeout);
}

void
RequestBackupDelegate::AdvanceCounter()
{
    _sequence_number = _pre_prepare->sequence + 1;
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
