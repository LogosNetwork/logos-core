/// @file
/// This file contains specializations of the ConsensusConnection class, which
/// handle the specifics of BatchBlock consensus.
#include <logos/consensus/batchblock/bb_consensus_connection.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/lib/epoch_time_util.hpp>

#include <random>

BBConsensusConnection::BBConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        PrimaryDelegate & primary,
        Promoter & promoter,
        MessageValidator & validator,
        const DelegateIdentities & ids,
        Service & service,
        EpochEventsNotifier & events_notifier,
        PersistenceManager<BSBCT> & persistence_manager)
    : Connection(iochannel, primary, promoter,
                 validator, ids, events_notifier, persistence_manager)
    , _timer(service)
{
    promoter.GetStore().batch_tip_get(_delegate_ids.remote, _prev_pre_prepare_hash);
}

/// Validate BatchStateBlock message.
///
///     @param message message to validate
///     @return true if validated false otherwise
bool
BBConsensusConnection::DoValidate(
    const PrePrepare & message)
{
    if(!ValidateSequence(message))
    {
        return false;
    }

    if(!ValidateRequests(message))
    {
        return false;
    }

    return true;
}

bool
BBConsensusConnection::ValidateSequence(
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
BBConsensusConnection::ValidateRequests(
    const PrePrepare & message)
{
    bool valid = true;

    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        if(!_persistence_manager.Validate(static_cast<const Request&>(message.blocks[i])))
        {
            _rejection_map[i] = 1;

            if(valid)
            {
                _reason = RejectionReason::Contains_Invalid_Request;
                valid = false;
            }
        }
    }

    return valid;
}

/// Commit the block to the database.
///
///     @param block to commit to the database
///     @param remote delegate id
void
BBConsensusConnection::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block, delegate_id);
}

bool
BBConsensusConnection::IsPrePrepared(
    const logos::block_hash & hash)
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
BBConsensusConnection::DoUpdateMessage(Rejection & message)
{
    message.reason = _reason;
    message.rejection_map = _rejection_map;
}

void
BBConsensusConnection::Reject()
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
        SendMessage<Rejection>();
        break;
    }
}

void
BBConsensusConnection::HandleReject(const PrePrepare & message)
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
//       stay with the backup (ConsensusConnection) and are only
//       transferred when fallback consensus is to take place, in
//       which case they are transferred to the primary list
//       (RequestHandler).
void
BBConsensusConnection::HandlePrePrepare(const PrePrepare & message)
{
    _pre_prepare_hashes.clear();

    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        _pre_prepare_hashes.insert(message.blocks[i].hash());
    }

    // to make sure during epoch transition, a fallback session of the new epoch
    // is not rerun by the old epoch, the min timeout should be > clock_drift (i.e. 20seconds)
    ScheduleTimer(GetTimeout(TIMEOUT_MIN, TIMEOUT_RANGE));
}

void
BBConsensusConnection::ScheduleTimer(Seconds timeout)
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
BBConsensusConnection::OnPostCommit()
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
BBConsensusConnection::OnPrePrepareTimeout(const Error & error)
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
BBConsensusConnection::ResetRejectionStatus()
{
    _reason = RejectionReason::Void;
    _rejection_map.reset();
}

bool
BBConsensusConnection::IsSubset(const PrePrepare & message)
{
    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        if(_pre_prepare_hashes.find(message.blocks[i].hash()) ==
                _pre_prepare_hashes.end())
        {
            return false;
        }
    }

    return true;
}

bool
BBConsensusConnection::ValidateReProposal(const PrePrepare & message)
{
    return IsSubset(message);
}

BBConsensusConnection::Seconds
BBConsensusConnection::GetTimeout(uint8_t min, uint8_t range)
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

template<>
template<>
void
ConsensusConnection<ConsensusType::BatchStateBlock>::UpdateMessage(Rejection & message)
{
    static_cast<BBConsensusConnection *>(this)
            ->DoUpdateMessage(message);
}
