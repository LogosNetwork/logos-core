#include <logos/consensus/backup_delegate.hpp>
#include <logos/network/consensus_netio.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>

#include <boost/asio/read.hpp>

template<ConsensusType CT>
BackupDelegate<CT>::BackupDelegate(std::shared_ptr<IOChannel> iochannel,
                                   std::shared_ptr<PrimaryDelegate> primary,
                                   MessagePromoter<CT> & promoter,
                                   MessageValidator & validator,
                                   const DelegateIdentities & ids,
                                   std::shared_ptr<EpochEventsNotifier> events_notifier,
                                   PersistenceManager<CT> & persistence_manager,
                                   p2p_interface & p2p,
                                   Service & service)
    : DelegateBridge<CT>(service, iochannel, p2p, ids.local)
    , _delegate_ids(ids)
    , _reason(RejectionReason::Void)
    , _validator(validator)
    , _primary(primary)
    , _promoter(promoter)
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
    _pre_prepare_timestamp = message.timestamp;
    _pre_prepare_hash = message.Hash();

    if(ProceedWithMessage(message, ConsensusState::VOID))
    {
        _state = ConsensusState::PREPARE;

        SetPrePrepare(message);

        HandlePrePrepare(message);

        PrepareMessage<CT> msg(_pre_prepare_hash);
        _validator.Sign(_pre_prepare_hash, msg.signature);
        SendMessage<PrepareMessage<CT>>(msg);
    }
    else
    {
        HandleReject(message);
        Reject();
        ResetRejectionStatus();
    }
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const PostPrepare & message)
{

    if(ProceedWithMessage(message, ConsensusState::PREPARE))
    {
        _post_prepare_hash = message.ComputeHash();
        _post_prepare_sig = message.signature;
        _state = ConsensusState::COMMIT;

        CommitMessage<CT> msg(_pre_prepare_hash);
        _validator.Sign(_post_prepare_hash, msg.signature);
        SendMessage<CommitMessage<CT>>(msg);
        LOG_DEBUG(_log) << __func__ << "<" << ConsensusToName(CT) << "> sent commit";
    }
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const PostCommit & message)
{
    auto notifier = _events_notifier.lock();
    if (!notifier)
    {
        return;
    }
    if(ProceedWithMessage(message))
    {
        assert(_pre_prepare);
        _post_commit_sig = message.signature;
        ApprovedBlock block(*_pre_prepare, _post_prepare_sig, _post_commit_sig);
        OnPostCommit();
        ApplyUpdates(block, _delegate_ids.remote);
        BlocksCallback::Callback<CT>(block);

        _state = ConsensusState::VOID;
        SetPreviousPrePrepareHash(_pre_prepare_hash);

        notifier->OnPostCommit(_pre_prepare->epoch_number);

        std::vector<uint8_t> buf;
        block.Serialize(buf, true, true);
        this->Broadcast(buf.data(), buf.size(), block.type);
    }
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const Prepare & message)
{
    auto primary = _primary.lock();
    if (!primary)
    {
        return;
    }
    primary->OnConsensusMessage(message, _delegate_ids.remote);
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const Commit & message)
{
    auto primary = _primary.lock();
    if (!primary)
    {
        return;
    }
    primary->OnConsensusMessage(message, _delegate_ids.remote);
}

template<ConsensusType CT>
void BackupDelegate<CT>::OnConsensusMessage(const Rejection & message)
{
    auto primary = _primary.lock();
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
        LOG_DEBUG(_log) << " BackupDelegate<CT>::Validate wrong primary id "
                << " msg " << message.Hash().to_string()
                << " id in pre-perpare " << (uint)message.primary_delegate
                << " id by connection " << (uint)_delegate_ids.remote;

        _reason = RejectionReason::Invalid_Primary_Index;
        return false;
    }

    if(!_validator.Validate(message.Hash(), message.preprepare_sig, _delegate_ids.remote))
    {
        LOG_DEBUG(_log) << " BackupDelegate<CT>::Validate Bad_Signature "
                << " msg " << message.Hash().to_string()
                << " sig " << message.preprepare_sig.to_string()
                << " id " << (uint)_delegate_ids.remote;

        _reason = RejectionReason::Bad_Signature;
        return false;
    }

    if(message.previous != _prev_pre_prepare_hash)
    {
        LOG_DEBUG(_log) << " BackupDelegate<"<< ConsensusToName(CT)
                        << ">::Validate Invalid_Previous_Hash "
                        << message.previous.to_string() << " "
                        << _prev_pre_prepare_hash.to_string();
        _reason = RejectionReason::Invalid_Previous_Hash;
        return false;
    }

    if(!ValidateTimestamp(message))
    {
        LOG_DEBUG(_log) << " BackupDelegate<CT>::Validate Clock_Drift";
        _reason = RejectionReason::Clock_Drift;
        return false;
    }

    if(_state == ConsensusState::PREPARE && !ValidateReProposal(message))
    {
        LOG_DEBUG(_log) << " BackupDelegate<CT>::Validate _state == ConsensusState::PREPARE && !ValidateReProposal(message)";
        return false;
    }

    if(!DoValidate(message))
    {
        LOG_DEBUG(_log) << " BackupDelegate<CT>::Validate DoValidate failed";
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
            LOG_DEBUG(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::Validate "
                            << " invalid Post_Prepare, pre_prepare hash " << _pre_prepare_hash.to_string()
                            << ", message pre_prepare hash " << message.preprepare_hash.to_string();
        }
        assert(_pre_prepare_hash==message.preprepare_hash);
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
        if(_state == ConsensusState::COMMIT)
        {
            if (_pre_prepare_hash != message.preprepare_hash)
            {
                LOG_DEBUG(_log) << "BackupDelegate<" << ConsensusToName(CT) << ">::Validate "
                                << " invalid Post_Commit, pre_prepare hash " << _pre_prepare_hash.to_string()
                                << ", message pre_prepare hash " << message.preprepare_hash.to_string();
            }
            assert(_pre_prepare_hash==message.preprepare_hash);
            return _validator.Validate(_post_prepare_hash, message.signature);
        }

        // We received the PostCommit without
        // having sent a commit message. We're
        // out of synch, but we can still validate
        // the message.
        // return ValidateSignature(message);
        //Peng: discussed with Devon. At this point, we are missing information
        // to create a post-committed block. So we should drop the message and
        // hope to get the post-committed block via bootstrap or p2p.
    }

    LOG_ERROR(_log) << "BackupDelegate - Attempting to validate "
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
    auto notifier = _events_notifier.lock();
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
         (delegate == EpochTransitionDelegate::Persistent && // Persistent from new Delegate's set
            connect == EpochConnection::Transitioning ||
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
        LOG_INFO(_log) << "BackupDelegate - Received " << MessageToName(message)
                       << " message while in " << StateToString(_state);
    }

    if(!Validate(message))
    {
        LOG_INFO(_log) << "BackupDelegate - Received " << MessageToName(message)
                       << ", Validate failed";
        return false;
    }

    // Epoch's validation must be the last, if it fails the request (currently BSB PrePrepare only)
    // is added with T(10,20) timer to the secondary list, therefore PrePrepare must be valid
    // TODO epoch # must be changed, hash recalculated, and signed
    if (!ValidateEpoch(message))
    {
        LOG_INFO(_log) << "BackupDelegate - Received " << MessageToName(message)
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
        LOG_INFO(_log) << "BackupDelegate - Proceeding with Post_Commit"
                       << " message received while in " << StateToString(_state);
    }

    if(Validate(message))
    {
        _sequence_number++;

        return true;
    }

    return false;
}

template<ConsensusType CT>
void BackupDelegate<CT>::HandlePrePrepare(const PrePrepare & message)
{}

template<ConsensusType CT>
void BackupDelegate<CT>::OnPostCommit()
{
    _promoter.OnPostCommit(*_pre_prepare);
}

template<ConsensusType CT>
void BackupDelegate<CT>::Reject()
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
