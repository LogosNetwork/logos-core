#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/network/consensus_netio.hpp>
#include <logos/consensus/consensus_connection.hpp>

#include <boost/asio/read.hpp>

template<ConsensusType CT>
ConsensusConnection<CT>::ConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                                             PrimaryDelegate & primary,
                                             RequestPromoter<CT> & promoter,
                                             MessageValidator & validator,
                                             const DelegateIdentities & ids,
                                             BlocksCallback & blocks_callback)
    : _iochannel(iochannel)
    , _delegate_ids(ids)
    , _validator(validator)
    , _primary(primary)
    , _promoter(promoter)
    , _blocks_callback(blocks_callback)
{}

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
        case MessageType::Key_Advert:
        case MessageType::Unknown:
            BOOST_LOG(_log) << "ConsensusConnection - Error - Received "
                            << MessageToName(type)
                            << " message type";
            break;
        default:
            BOOST_LOG(_log) << "ConsensusConnection - Error - Received invalid message type "
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

    BOOST_LOG(_log) << "ConsensusConnection<"
                    << ConsensusToName(CT) << ">- Received "
                    << MessageToName(type)
                    << " message.";

    switch (type)
    {
        case MessageType::Pre_Prepare: {
            auto msg (*reinterpret_cast<const PrePrepare*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Prepare: {
            auto msg (*reinterpret_cast<const Prepare*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Prepare: {
            auto msg (*reinterpret_cast<const PostPrepare*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Commit: {
            auto msg (*reinterpret_cast<const Commit*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Commit: {
            auto msg (*reinterpret_cast<const PostCommit*>(_receive_buffer.data()));
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
void ConsensusConnection<CT>::OnConsensusMessage(const PrePrepare & message)
{
    if(ProceedWithMessage(message, ConsensusState::VOID))
    {
        _state = ConsensusState::PREPARE;

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _cur_pre_prepare.reset(new PrePrepare(message));
        }

        OnPrePrepare(*_cur_pre_prepare);

        _cur_pre_prepare_hash = message.Hash();

        SendMessage<PrepareMessage<CT>>();
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
        assert(_cur_pre_prepare);

        ApplyUpdates(*_cur_pre_prepare, _delegate_ids.remote);
        _blocks_callback.NotifyClient(*_cur_pre_prepare);
        _state = ConsensusState::VOID;
    }
}

template<ConsensusType CT>
template<MessageType MT>
void ConsensusConnection<CT>::OnConsensusMessage(const SPMessage<MT> & message)
{
    _primary.OnConsensusMessage(message, _delegate_ids.remote);
}

template<ConsensusType CT>
template<typename MSG>
bool ConsensusConnection<CT>::Validate(const MSG & message)
{
    if(_state == ConsensusState::PREPARE)
    {
        return _validator.Validate(message, *_cur_prepare);
    }

    if(_state == ConsensusState::COMMIT)
    {
        return _validator.Validate(message, *_cur_commit);
    }

    BOOST_LOG(_log) << "ConsensusConnection - Attempting to validate "
                    << MessageToName(message) << " while in "
                    << StateToString(_state);

    return false;
}

template<ConsensusType CT>
template<typename MSG>
bool ConsensusConnection<CT>::ProceedWithMessage(const MSG & message,
                                                 ConsensusState expected_state)
{
    if(_state != expected_state)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Received " << MessageToName(message)
                        << " message while in " << StateToString(_state);
    }

    if(Validate(message))
    {
        return true;
    }

    return false;
}

template<ConsensusType CT>
bool ConsensusConnection<CT>::ProceedWithMessage(const PostCommit & message)
{
    if(_state != ConsensusState::COMMIT)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Proceeding with Post_Commit"
                        << " message received while in " << StateToString(_state);
    }

    if(Validate(message))
    {
        return true;
    }

    return false;
}

template<ConsensusType CT>
template<typename MSG>
void ConsensusConnection<CT>::SendMessage()
{
    MSG response(_cur_pre_prepare->timestamp);

    response.previous = _cur_pre_prepare_hash;
    _validator.Sign(response);

    StoreResponse(response);

    Send(response);
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
    _cur_prepare.reset(new Prepare(message));
}

template<ConsensusType CT>
void ConsensusConnection<CT>::StoreResponse(const Commit & message)
{
    _cur_commit.reset(new Commit(message));
}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnPrequel(const uint8_t *data)
{
    std::memcpy(_receive_buffer.data(), data, sizeof(Prequel));
    OnData();
}

template<ConsensusType CT>
void ConsensusConnection<CT>::OnPrePrepare(const PrePrepare & message)
{
    _promoter.OnPrePrepare(message);
}

template class ConsensusConnection<ConsensusType::BatchStateBlock>;
template class ConsensusConnection<ConsensusType::MicroBlock>;
template class ConsensusConnection<ConsensusType::Epoch>;

