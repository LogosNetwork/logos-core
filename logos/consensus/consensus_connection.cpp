#include <logos/consensus/consensus_connection.hpp>

#include <logos/consensus/network/consensus_netio.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>

#include <boost/asio/read.hpp>

template<ConsensusType CT>
ConsensusConnection<CT>::ConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                                             PrimaryDelegate & primary,
                                             RequestPromoter<CT> & promoter,
                                             MessageValidator & validator,
                                             const DelegateIdentities & ids,
                                             EpochEventsNotifier & events_notifer)
    : _iochannel(iochannel)
    , _delegate_ids(ids)
    , _reason(RejectionReason::Void)
    , _validator(validator)
    , _primary(primary)
    , _promoter(promoter)
    , _events_notifier(events_notifer)
{
}

template<ConsensusType CT>
void ConsensusConnection<CT>::Send(const void * data, size_t size)
{
    _iochannel->Send(data, size);
}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnData()
{
    MessageType type (static_cast<MessageType> (_receive_buffer.data()[1]));

    switch (type)
    {
        case MessageType::Pre_Prepare:
            _iochannel->AsyncRead(sizeof(PrePrepare) -
                                  sizeof(Prequel),
                                  std::bind(&ConsensusConnection<CT>::OnMessage, this,
                                            std::placeholders::_1));
            break;
        case MessageType::Prepare:
            _iochannel->AsyncRead(sizeof(Prepare) -
                                  sizeof(Prequel),
                                  std::bind(&ConsensusConnection<CT>::OnMessage, this,
                                            std::placeholders::_1));
            break;
        case MessageType::Post_Prepare:
            _iochannel->AsyncRead(sizeof(PostPrepare) -
                                  sizeof(Prequel),
                                  std::bind(&ConsensusConnection<CT>::OnMessage, this,
                                            std::placeholders::_1));
            break;
        case MessageType::Commit:
            _iochannel->AsyncRead(sizeof(Commit) -
                                  sizeof(Prequel),
                                  std::bind(&ConsensusConnection<CT>::OnMessage, this,
                                            std::placeholders::_1));
            break;
        case MessageType::Post_Commit:
            _iochannel->AsyncRead(sizeof(PostCommit) -
                                  sizeof(Prequel),
                                   std::bind(&ConsensusConnection<CT>::OnMessage, this,
                                             std::placeholders::_1));
            break;
        case MessageType::Rejection:
            _iochannel->AsyncRead(sizeof(Rejection) -
                                  sizeof(Prequel),
                                   std::bind(&ConsensusConnection<CT>::OnMessage, this,
                                             std::placeholders::_1));
            break;
        case MessageType::Key_Advert:
        case MessageType::Unknown:
            LOG_ERROR(_log) << "ConsensusConnection - Received "
                            << MessageToName(type)
                            << " message type";
            break;
        default:
            LOG_ERROR(_log) << "ConsensusConnection - Received invalid message type "
                            << (int)(_receive_buffer.data()[1]);
            break;
    }
}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnMessage(const uint8_t * data)
{
    MessageType type (static_cast<MessageType> (_receive_buffer.data()[1]));

    memcpy(_receive_buffer.data() + sizeof(Prequel), data,
           MessageTypeToSize<CT>(type) - sizeof(Prequel));

    std::string message = MessageToName(type);
    if (type == MessageType::Rejection)
    {
        auto msg (*reinterpret_cast<const Rejection*>(_receive_buffer.data()));
        message += ":" + RejectionReasonToName(msg.reason);
    }
    LOG_DEBUG(_log) << "ConsensusConnection<"
                    << ConsensusToName(CT) << ">- Received "
                    << message
                    << " message.";

    switch (type)
    {
        case MessageType::Pre_Prepare:
        {
            auto msg (*reinterpret_cast<const PrePrepare*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Prepare:
        {
            auto msg (*reinterpret_cast<const Prepare*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Prepare:
        {
            auto msg (*reinterpret_cast<const PostPrepare*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Commit:
        {
            auto msg (*reinterpret_cast<const Commit*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Commit:
        {
            auto msg (*reinterpret_cast<const PostCommit*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Rejection:
        {
            auto msg (*reinterpret_cast<const Rejection*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Key_Advert:
        case MessageType::Unknown:
            break;
    }

    _iochannel->ReadPrequel();
}

template<ConsensusType CT>
void ConsensusConnection<CT>::SetPrePrepare(const PrePrepare & message)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _pre_prepare.reset(new PrePrepare(message));
}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnConsensusMessage(const PrePrepare & message)
{
    _pre_prepare_timestamp = message.timestamp;
    _pre_prepare_hash = message.Hash();

    if(ProceedWithMessage(message, ConsensusState::VOID))
    {
        _state = ConsensusState::PREPARE;

        SetPrePrepare(message);

        HandlePrePrepare(message);
        SendMessage<PrepareMessage<CT>>();
    }
    else
    {
        HandleReject(message);
        Reject();
        ResetRejectionStatus();
    }
}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnConsensusMessage(const PostPrepare & message)
{
    if(ProceedWithMessage(message, ConsensusState::PREPARE))
    {
        _state = ConsensusState::COMMIT;

        SendMessage<CommitMessage<CT>>();
    }
}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnConsensusMessage(const PostCommit & message)
{
    if(ProceedWithMessage(message))
    {
        assert(_pre_prepare);

        OnPostCommit();
        ApplyUpdates(*_pre_prepare, _delegate_ids.remote);

        _state = ConsensusState::VOID;
        _prev_pre_prepare_hash = _pre_prepare_hash;

        _events_notifier.OnPostCommit(_pre_prepare->epoch_number);
    }
}

template<ConsensusType CT>
template<typename M>
void ConsensusConnection<CT>::OnConsensusMessage(const M & message)
{
    _primary.OnConsensusMessage(message, _delegate_ids.remote);
}

template<ConsensusType CT>
bool ConsensusConnection<CT>::Validate(const PrePrepare & message)
{
    if(!_validator.Validate(message, _delegate_ids.remote))
    {
        _reason = RejectionReason::Bad_Signature;
        return false;
    }

    if(message.previous != _prev_pre_prepare_hash)
    {
        _reason = RejectionReason::Invalid_Previous_Hash;
        return false;
    }

    if(!ValidateTimestamp(message))
    {
        _reason = RejectionReason::Clock_Drift;
        return false;
    }

    if(_state == ConsensusState::PREPARE && !ValidateReProposal(message))
    {
        return false;
    }

    if(!DoValidate(message))
    {
        return false;
    }

    return true;
}

template<ConsensusType CT>
template<typename M>
bool ConsensusConnection<CT>::Validate(const M & message)
{
    if(message.type == MessageType::Post_Prepare)
    {
        return ValidateSignature(message, *_prepare);
    }

    if(message.type == MessageType::Post_Commit)
    {
        if(_state == ConsensusState::COMMIT)
        {
            return ValidateSignature(message, *_commit);
        }

        // We received the PostCommit without
        // having sent a commit message. We're
        // out of synch, but we can still validate
        // the message.
        return ValidateSignature(message);
    }

    LOG_ERROR(_log) << "ConsensusConnection - Attempting to validate "
                    << MessageToName(message) << " while in "
                    << StateToString(_state);

    return false;
}

template<ConsensusType CT>
template<typename M, typename S>
bool ConsensusConnection<CT>::ValidateSignature(const M & m, const S & s)
{
    if(!_validator.Validate(m, s))
    {
        _reason = RejectionReason::Bad_Signature;
        return false;
    }

    return true;
}

template<ConsensusType CT>
template<typename M>
bool ConsensusConnection<CT>::ValidateSignature(const M & m)
{
    if(!_validator.Validate(m))
    {
        _reason = RejectionReason::Bad_Signature;
        return false;
    }

    return true;
}

template<ConsensusType CT>
bool ConsensusConnection<CT>::ValidateTimestamp(const PrePrepare & message)
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
bool ConsensusConnection<ConsensusType::BatchStateBlock>::ValidateEpoch(
    const PrePrepareMessage<ConsensusType::BatchStateBlock> &message)
{
    bool valid = true;

    auto delegate = _events_notifier.GetDelegate();
    auto state = _events_notifier.GetState();
    if (delegate == EpochTransitionDelegate::PersistentReject ||
        delegate == EpochTransitionDelegate::RetiringForwardOnly)
    {
        _reason = RejectionReason::New_Epoch;
        valid = false;
    }
    else if (state == EpochTransitionState::Connecting &&
         (delegate == EpochTransitionDelegate::Persistent ||
          delegate == EpochTransitionDelegate::New))
    {
        _reason = RejectionReason::Invalid_Epoch;
        valid = false;
    }

    return valid;
}

template<ConsensusType CT>
template<typename M>
bool ConsensusConnection<CT>::ProceedWithMessage(const M & message,
                                                 ConsensusState expected_state)
{
    if(_state != expected_state)
    {
        LOG_INFO(_log) << "ConsensusConnection - Received " << MessageToName(message)
                       << " message while in " << StateToString(_state);
    }

    if(!Validate(message))
    {
        return false;
    }

    // Epoch's validation must be the last, if it fails the request (currently BSB PrePrepare only)
    // is added with T(10,20) timer to the secondary list, therefore PrePrepare must be valid
    // TODO epoch # must be changed, hash recalculated, and signed
    if (!ValidateEpoch(message))
    {
        return false;
    }

    return true;
}

template<ConsensusType CT>
bool ConsensusConnection<CT>::ProceedWithMessage(const PostCommit & message)
{
    if(_state != ConsensusState::COMMIT)
    {
        LOG_INFO(_log) << "ConsensusConnection - Proceeding with Post_Commit"
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
void ConsensusConnection<CT>::SendKeyAdvertisement()
{
    KeyAdvertisement advert;
    advert.public_key = _validator.GetPublicKey();
    Send(advert);
}

template<ConsensusType CT>
void ConsensusConnection<CT>::StoreResponse(const Prepare & message)
{
    _prepare.reset(new Prepare(message));
}

template<ConsensusType CT>
void ConsensusConnection<CT>::StoreResponse(const Rejection & message)
{}

template<ConsensusType CT>
void ConsensusConnection<CT>::StoreResponse(const Commit & message)
{
    _commit.reset(new Commit(message));
}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnPrequel(const uint8_t *data)
{
    std::memcpy(_receive_buffer.data(), data, sizeof(Prequel));
    OnData();
}

template<ConsensusType CT>
void ConsensusConnection<CT>::HandlePrePrepare(const PrePrepare & message)
{}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnPostCommit()
{
    _promoter.OnPostCommit(*_pre_prepare);
}

template<ConsensusType CT>
template<typename M>
void ConsensusConnection<CT>::UpdateMessage(M & message)
{}

template<ConsensusType CT>
void ConsensusConnection<CT>::Reject()
{}

template<ConsensusType CT>
void ConsensusConnection<CT>::ResetRejectionStatus()
{}

template<ConsensusType CT>
bool ConsensusConnection<CT>::ValidateReProposal(const PrePrepare & message)
{}

template class ConsensusConnection<ConsensusType::BatchStateBlock>;
template class ConsensusConnection<ConsensusType::MicroBlock>;
template class ConsensusConnection<ConsensusType::Epoch>;
