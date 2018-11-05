/// @file
/// This file contains specializations of the ConsensusConnection class, which
/// handle the specifics of BatchBlock consensus.
#include <logos/consensus/batchblock/bb_consensus_connection.hpp>
#include <logos/consensus/consensus_manager.hpp>

#include <random>

BBConsensusConnection::BBConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        PrimaryDelegate & primary,
        Promoter & promoter,
        PersistenceManager & persistence_manager,
        MessageValidator & validator,
        const DelegateIdentities & ids,
        Service & service)
    : Connection(iochannel, primary, promoter,
                 validator, ids)
    , _timer(service)
    , _persistence_manager(persistence_manager)
{}

/// Validate BatchStateBlock message.
///
///     @param message message to validate
///     @return true if validated false otherwise
bool
BBConsensusConnection::DoValidate(
    const PrePrepare & message)
{
    bool valid = true;

    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        if(!_persistence_manager.Validate(message.blocks[i]))
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
        SendMessage<Rejection>();
        break;
    }
}

// XXX - If a Primary delegate re-proposes a subset of transactions
//       and then fails to post commit the re-proposed batch, when
//       a backup initiates fallback consensus, it is possible that
//       a transaction omitted from the re-proposed batch is forgotten,
//       since individual requests are not stored for fallback consensus.
void
BBConsensusConnection::HandlePrePrepare(const PrePrepare & message)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(TIMEOUT_MIN,
                                        TIMEOUT_MAX);

    Seconds timeout(dis(gen));

    _pre_prepare_hashes.clear();

    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        _pre_prepare_hashes.insert(message.blocks[i].hash());
    }

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
BBConsensusConnection::HandlePostPrepare(const PostPrepare & message)
{
    std::lock_guard<std::mutex> lock(_timer_mutex);

    if(!_timer.cancel() && _callback_scheduled)
    {
        _cancel_timer = true;
        return;
    }

    _callback_scheduled = false;
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

template<>
template<>
void
ConsensusConnection<ConsensusType::BatchStateBlock>::UpdateMessage(Rejection & message)
{
    static_cast<BBConsensusConnection *>(this)
            ->DoUpdateMessage(message);
}
