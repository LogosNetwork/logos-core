#include <logos/consensus/backup_delegate.hpp>
#include <logos/network/consensus_netio.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>

#include <boost/asio/read.hpp>

template<ConsensusType CT>
BackupDelegate<CT>::BackupDelegate(std::shared_ptr<IOChannel> iochannel,
                                   std::shared_ptr<PrimaryDelegate> primary,
                                   Store & store,
                                   Cache & block_cache,
                                   MessageValidator & validator,
                                   const DelegateIdentities & ids,
                                   ConsensusScheduler & scheduler,
                                   std::shared_ptr<EpochEventsNotifier> events_notifier,
                                   PersistenceManager<CT> & persistence_manager,
                                   p2p_interface & p2p,
                                   Service & service)
    : DelegateBridge<CT>(service, iochannel, p2p, ids.local)
    , _delegate_ids(ids)
    , _reason(RejectionReason::Void)
    , _validator(validator)
    , _primary(primary)
    , _store(store)
    , _block_cache(block_cache)
    , _scheduler(scheduler)
    , _events_notifier(events_notifier)
    , _persistence_manager(persistence_manager)
    , _epoch_number(primary->GetEpochNumber())
{}

template<ConsensusType CT>
void BackupDelegate<CT>::SetPrePrepare(const PrePrepare & message)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _pre_prepare.reset(new PrePrepare(message));
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const PrePrepare & message)
{
    auto hash = message.Hash();
    // Have we already seen this hash this round? If so, only rebroadcast prepare for the old message
    if (hash == _pre_prepare_hash)
    {
        // Having advanced to PREPARE or COMMIT means we previously approved the pre_prepare
        if (_state == ConsensusState::PREPARE || _state == ConsensusState::COMMIT)
        {
            PrepareMessage<CT> msg(hash);
            _validator.Sign(hash, msg.signature);

            SendMessage<PrepareMessage<CT>>(msg);
            LOG_DEBUG(_log) << "BackupDelegate<" << ConsensusToName(CT)
                            << ">::OnConsensusMessage - Re-broadcast Prepare";
            return;
        }
        // state VOID: we might have previously rejected it, try again to see if approval conditions are now satisfied
    }
    // Ignore if it's an old block
    else if (IsOldBlock(message))
    {
        LOG_DEBUG(_log) << "BackupDelegate<" << ConsensusToName(CT)
                        << ">::OnConsensusMessage - Old block " << hash.to_string();
        return;
    }

    // Ignore if not in p2p2 mode and timestamp check fails
    if(!ValidateTimestamp(message) && !this->P2pEnabled())
    {
        LOG_DEBUG(_log) << " BackupDelegate<" << ConsensusToName(CT) << ">::Validate - Clock_Drift";
        return;
    }

    if(ProceedWithMessage(message, ConsensusState::VOID))
    {
        // Should only overwrite pre_prepare hash and timestamp tracker if message is valid
        _pre_prepare_timestamp = message.timestamp;
        _pre_prepare_hash = hash;

        _state = ConsensusState::PREPARE;

        SetPrePrepare(message);

        HandlePrePrepare(message);

        PrepareMessage<CT> msg(hash);
        _validator.Sign(hash, msg.signature);
        LOG_DEBUG(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::OnConsensusMessage - Sign";
        SendMessage<PrepareMessage<CT>>(msg);
    }
    else
    {
        HandleReject(message);
        Reject(hash);
        ResetRejectionStatus();
    }
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const PostPrepare & message)
{
    auto hash = message.ComputeHash();
    if (hash == _post_prepare_hash)
    {
        if (_state == ConsensusState::VOID || _state == ConsensusState::PREPARE)
        {
            LOG_FATAL(_log) << "BackupDelegate<" << ConsensusToName(CT)
                            << ">::OnConsensusMessage - PostPrepare already seen but in wrong internal state: "
                            << StateToString(_state);
            trace_and_halt();
        }
        CommitMessage<CT> msg(hash);
        _validator.Sign(_post_prepare_hash, msg.signature);
        SendMessage<CommitMessage<CT>>(msg);
        LOG_DEBUG(_log) << "BackupDelegate<" << ConsensusToName(CT)
                        << ">::OnConsensusMessage - Re-broadcast Commit";
        return;
    }

    if(ProceedWithMessage(message, ConsensusState::PREPARE))
    {
        _post_prepare_hash = hash;
        _post_prepare_sig = message.signature;
        _state = ConsensusState::COMMIT;

        CommitMessage<CT> msg(_pre_prepare_hash);
        _validator.Sign(_post_prepare_hash, msg.signature);
        SendMessage<CommitMessage<CT>>(msg);
        LOG_DEBUG(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::" << __func__ << " - sent commit";
    }
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const PostCommit & message)
{
    auto notifier = GetSharedPtr(_events_notifier, "BackupDelegate<", ConsensusToName(CT),
            ">::OnConsensusMessage, object destroyed");
    if (!notifier)
    {
        return;
    }

    if(ProceedWithMessage(message))
    {
        assert(_pre_prepare);
        _post_commit_sig = message.signature;
        ApprovedBlock block(*_pre_prepare, _post_prepare_sig, _post_commit_sig);
        // Must apply to DB before clearing from queue so that Archiver can fetch latest microblock sequence
        ApplyUpdates(block, _delegate_ids.remote);
        OnPostCommit();
        BlocksCallback::Callback<CT>(block);

        _state = ConsensusState::VOID;
        SetPreviousPrePrepareHash(_pre_prepare_hash);
        AdvanceCounter();
        _pre_prepare_hash.clear();
        _post_prepare_sig.clear();
        _post_commit_sig.clear();
        _post_prepare_hash.clear();

        notifier->OnPostCommit(_pre_prepare->epoch_number);

        std::vector<uint8_t> buf;
        block.Serialize(buf, true, true);
        this->Broadcast(buf.data(), buf.size(), block.type);
    }
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const Prepare & message)
{
    auto primary = GetSharedPtr(_primary, "BackupDelegate<", ConsensusToName(CT),
            ">::OnConsensusMessage, object destroyed");
    if (!primary)
    {
        return;
    }
    primary->OnConsensusMessage(message, _delegate_ids.remote);
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const Commit & message)
{
    auto primary = GetSharedPtr(_primary, "BackupDelegate<", ConsensusToName(CT),
                                ">::OnConsensusMessage, object destroyed");
    if (!primary)
    {
        return;
    }
    primary->OnConsensusMessage(message, _delegate_ids.remote);
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const Rejection & message)
{
    auto primary = GetSharedPtr(_primary, "BackupDelegate<", ConsensusToName(CT),
                                ">::OnConsensusMessage, object destroyed");
    if (!primary)
    {
        return;
    }
    primary->OnConsensusMessage(message, _delegate_ids.remote);
}

template<ConsensusType CT>
bool BackupDelegate<CT>::Validate(const PrePrepare & message)
{
    // TODO: Once ID management is ready, we have to check if signature and primary_delegate match
    if(message.primary_delegate != _delegate_ids.remote)
    {
        LOG_DEBUG(_log) << " BackupDelegate<" << ConsensusToName(CT) << ">::Validate wrong primary id "
                << " msg " << message.Hash().to_string()
                << " id in pre-perpare " << (uint)message.primary_delegate
                << " id by connection " << (uint)_delegate_ids.remote;

        _reason = RejectionReason::Invalid_Primary_Index;
        return false;
    }

    if(!_validator.Validate(message.Hash(), message.preprepare_sig, _delegate_ids.remote))
    {
        LOG_DEBUG(_log) << " BackupDelegate<" << ConsensusToName(CT) << ">::Validate Bad_Signature "
                << " msg " << message.Hash().to_string()
                << " sig " << message.preprepare_sig.to_string()
                << " id " << (uint)_delegate_ids.remote;

        _reason = RejectionReason::Bad_Signature;
        return false;
    }

     // TODO: potentially need to bootstrap here as we might be behind!
    if(message.previous != _prev_pre_prepare_hash)
    {
        LOG_DEBUG(_log) << " BackupDelegate<"<< ConsensusToName(CT)
                        << ">::Validate Invalid_Previous_Hash "
                        << message.previous.to_string() << " "
                        << _prev_pre_prepare_hash.to_string();
        _reason = RejectionReason::Invalid_Previous_Hash;
        return false;
    }


    if(_state == ConsensusState::PREPARE && !ValidateReProposal(message))
    {
        LOG_DEBUG(_log) << " BackupDelegate<" << ConsensusToName(CT) << ">::Validate _state == ConsensusState::PREPARE && !ValidateReProposal(message)";
        return false;
    }

    if(!DoValidate(message))
    {
        LOG_DEBUG(_log) << " BackupDelegate<" << ConsensusToName(CT) << ">::Validate DoValidate failed";
        return false;
    }

    return true;
}

template<ConsensusType CT>
template<typename M>
bool BackupDelegate<CT>::Validate(const M & message)
{
    if(message.type == MessageType::Post_Prepare)
    {
        if (_pre_prepare_hash != message.preprepare_hash)
        {
            LOG_WARN(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::Validate "
                           << " invalid Post_Prepare, pre_prepare hash " << _pre_prepare_hash.to_string()
                           << ", message pre_prepare hash " << message.preprepare_hash.to_string();
            // TODO: bootstrap here
            return false;
        }
        auto good = _validator.Validate(_pre_prepare_hash, message.signature);
        if(!good)
        {
            LOG_DEBUG(_log) << "_validator.Validate(_pre_prepare_hash, message.signature) failed. "
                    << _pre_prepare_hash.to_string() << " "
                    << message.preprepare_hash.to_string() << " "
                    << message.signature.sig.to_string() << " "
                    << message.signature.map.to_string();
        }
        return good;
    }

    if(message.type == MessageType::Post_Commit)
    {
        // must be in COMMIT since ProceedWithMessage guaranteed that; can remove later
        assert(_state == ConsensusState::COMMIT);

        if (_pre_prepare_hash != message.preprepare_hash)
        {
            LOG_WARN(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::Validate "
                           << " invalid Post_Commit, pre_prepare hash " << _pre_prepare_hash.to_string()
                           << ", message pre_prepare hash " << message.preprepare_hash.to_string();
            return false;
        }
        return _validator.Validate(_post_prepare_hash, message.signature);

        // We received the PostCommit without
        // having sent a commit message. We're
        // out of synch, but we can still validate
        // the message.
        // return ValidateSignature(message);
        //Peng: discussed with Devon. At this point, we are missing information
        // to create a post-committed block. So we should drop the message and
        // hope to get the post-committed block via bootstrap or p2p.
    }

    LOG_ERROR(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::Validate - Attempting to validate "
                    << MessageToName(message) << " while in "
                    << StateToString(_state);

    return false;
}

template<ConsensusType CT>
bool BackupDelegate<CT>::ValidateTimestamp(const PrePrepare & message)
{
    auto now = GetStamp();
    auto ts = message.timestamp;

    auto drift = now > ts ? now - ts : ts - now;

    if(drift > MAX_CLOCK_DRIFT_MS)
    {
        return false;
    }

    return true;
}

template<>
template<>
bool BackupDelegate<ConsensusType::Request>::ValidateEpoch(
    const PrePrepareMessage<ConsensusType::Request> &message)
{
    auto notifier = GetSharedPtr(_events_notifier, "BackupDelegate<Request>::ValidateEpoch, object destroyed");
    if (!notifier)
    {
        return false;
    }
    bool valid = true;

    auto delegate = notifier->GetDelegate();
    auto state = notifier->GetState();
    auto connect = notifier->GetConnection();
    if (delegate == EpochTransitionDelegate::PersistentReject ||
        delegate == EpochTransitionDelegate::RetiringForwardOnly)
    {
        _reason = RejectionReason::New_Epoch;
        valid = false;
    }
    else if (state == EpochTransitionState::Connecting &&
            ((delegate == EpochTransitionDelegate::Persistent && // Persistent from new Delegate's set
              connect == EpochConnection::Transitioning) ||
             delegate == EpochTransitionDelegate::New))
    {
        _reason = RejectionReason::Invalid_Epoch;
        valid = false;
    }

    return valid;
}

template<ConsensusType CT>
template<typename M>
bool BackupDelegate<CT>::ProceedWithMessage(const M & message,
                                            ConsensusState expected_state)
{
    if(_state != expected_state)
    {
        LOG_INFO(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::ProceedWithMessage - Received " << MessageToName(message)
                       << " message while in " << StateToString(_state);
        return false;  // TODO: bootstrap here
    }

    if(!Validate(message))
    {
        LOG_INFO(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::ProceedWithMessage - Received " << MessageToName(message)
                       << ", Validate failed";
        return false;
    }

    // Epoch's validation must be the last, if it fails the request (currently BSB PrePrepare only)
    // is added with T(10,20) timer to the secondary list, therefore PrePrepare must be valid
    // TODO epoch # must be changed, hash recalculated, and signed
    if (!ValidateEpoch(message))
    {
        LOG_INFO(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::ProceedWithMessage - Received " << MessageToName(message)
                       << ", ValidateEpoch failed";
        return false;
    }

    return true;
}

template<ConsensusType CT>
bool BackupDelegate<CT>::ProceedWithMessage(const PostCommit & message)
{
    if(_state != ConsensusState::COMMIT)
    {
        LOG_INFO(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::ProceedWithMessage - Proceeding with PostCommit"
                       << " message received while in " << StateToString(_state);
        return false;
    }

    return Validate(message);
}

template<ConsensusType CT>
void BackupDelegate<CT>::HandlePrePrepare(const PrePrepare & message)
{}

template<ConsensusType CT>
void BackupDelegate<CT>::OnPostCommit()
{
    GetHandler().OnPostCommit(_pre_prepare);
}

template<ConsensusType CT>
bool BackupDelegate<CT>::IsOldBlock(const PrePrepare & message)
{
    return message.epoch_number < _expected_epoch_number ||
           (message.epoch_number == _expected_epoch_number && message.sequence < _sequence_number);
}

template<ConsensusType CT>
void BackupDelegate<CT>::Reject(const BlockHash &)
{}

template<ConsensusType CT>
void BackupDelegate<CT>::ResetRejectionStatus()
{}

template<ConsensusType CT>
bool BackupDelegate<CT>::ValidateReProposal(const PrePrepare & message)
{
    return false;
}

template class BackupDelegate<ConsensusType::Request>;
template class BackupDelegate<ConsensusType::MicroBlock>;
template class BackupDelegate<ConsensusType::Epoch>;
