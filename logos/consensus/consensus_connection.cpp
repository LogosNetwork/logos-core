#include <logos/consensus/consensus_connection.hpp>

#include <logos/node/node.hpp>

#include <boost/asio/read.hpp>

template<ConsensusType consensus_type>
ConsensusConnection<consensus_type>::ConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
                                         logos::alarm & alarm,
                                         PrimaryDelegate * primary,
                                         PersistenceManager & persistence_manager,
                                         DelegateKeyStore & key_store,
                                         MessageValidator & validator,
                                         const DelegateIdentities & ids)
        : _iochannel(iochannel) 
        , _delegate_ids(ids)
        , _persistence_manager(persistence_manager)
        , _key_store(key_store)
        , _validator(validator)
        , _primary(primary)
        , _alarm(alarm)
{
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::Send(const void * data, size_t size)
{
    _iochannel->Send(data, size);
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::OnData(boost::system::error_code const & ec, size_t size)
{
    if(size != sizeof(Prequel))
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error, received " << size << " bytes";
        return;
    }

    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error receiving message prequel: " << ec.message();
        return;
    }

    MessageType type (static_cast<MessageType> (_receive_buffer.data()[1]));
    switch (type)
    {
        case MessageType::Pre_Prepare:
            _iochannel->AsyncRead(boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(PrePrepareMessage<consensus_type>) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection<consensus_type>::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Prepare:
            _iochannel->AsyncRead(boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(PrepareMessage<consensus_type>) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection<consensus_type>::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Post_Prepare:
            _iochannel->AsyncRead(boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(PostPrepareMessage<consensus_type>) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection<consensus_type>::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Commit:
            _iochannel->AsyncRead(boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(CommitMessage<consensus_type>) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection<consensus_type>::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Post_Commit:
            _iochannel->AsyncRead(boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(PostCommitMessage<consensus_type>) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection<consensus_type>::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Key_Advert:
            _iochannel->AsyncRead(boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(KeyAdvertisement) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection<consensus_type>::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Unknown:
            BOOST_LOG(_log) << "ConsensusConnection - Received unknown message type";
            break;
        default:
            break;
    }
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::OnMessage(boost::system::error_code const & ec, size_t size)
{
    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error receiving message: " << ec.message();
        return;
    }

    MessageType type (static_cast<MessageType> (_receive_buffer.data()[1]));

    BOOST_LOG(_log) << "ConsensusConnection - Received "
                    << MessageToName(type)
                    << " message.";

    switch (type)
    {
        case MessageType::Pre_Prepare: {
            auto msg (*reinterpret_cast<const PrePrepareMessage<consensus_type>*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Prepare: {
            auto msg (*reinterpret_cast<const PrepareMessage<consensus_type>*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Prepare: {
            auto msg (*reinterpret_cast<const PostPrepareMessage<consensus_type>*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Commit: {
            auto msg (*reinterpret_cast<const CommitMessage<consensus_type>*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Commit: {
            auto msg (*reinterpret_cast<const PostCommitMessage<consensus_type>*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Key_Advert: {
            auto msg (*reinterpret_cast<KeyAdvertisement*>(_receive_buffer.data()));
            _key_store.OnPublicKey(_delegate_ids.remote, msg.public_key);
            break;
        }
        case MessageType::Unknown:
            break;
    }

    _iochannel->ReadPrequel();
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::OnConsensusMessage(const PrePrepareMessage<consensus_type> & message)
{
    if(ProceedWithMessage(message, ConsensusState::VOID))
    {
        _state = ConsensusState::PREPARE;
        _cur_pre_prepare.reset(new PrePrepareMessage<consensus_type>(message));
        _cur_pre_prepare_hash = message.Hash();

        SendMessage<PrepareMessage<consensus_type>>();
    }
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::OnConsensusMessage(const PostPrepareMessage<consensus_type> & message)
{
    if(ProceedWithMessage(message, ConsensusState::PREPARE))
    {
        _state = ConsensusState::COMMIT;

        SendMessage<CommitMessage<consensus_type>>();
    }
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::OnConsensusMessage(const PostCommitMessage<consensus_type> & message)
{
    if(ProceedWithMessage(message))
    {
        assert(_cur_pre_prepare);

        ApplyUpdates(*_cur_pre_prepare, _delegate_ids.remote);
        _state = ConsensusState::VOID;
    }
}

template<ConsensusType consensus_type>
template<MessageType Type>
void ConsensusConnection<consensus_type>::OnConsensusMessage(const StandardPhaseMessage<Type, consensus_type> & message)
{
    _primary->OnConsensusMessage(message, _delegate_ids.remote);
}

template<ConsensusType consensus_type>
template<typename MSG>
bool ConsensusConnection<consensus_type>::Validate(const MSG & message)
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

template<ConsensusType consensus_type>
template<typename MSG>
bool ConsensusConnection<consensus_type>::ProceedWithMessage(const MSG & message, ConsensusState expected_state)
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

template<ConsensusType consensus_type>
bool ConsensusConnection<consensus_type>::ProceedWithMessage(const PostCommitMessage<consensus_type> & message)
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

template<ConsensusType consensus_type>
template<typename MSG>
void ConsensusConnection<consensus_type>::SendMessage()
{
    MSG response(_cur_pre_prepare->timestamp);

    response.hash = _cur_pre_prepare_hash;
    _validator.Sign(response);

    StoreResponse(response);

    Send(response);
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::SendKeyAdvertisement()
{
    KeyAdvertisement advert;
    advert.public_key = _validator.GetPublicKey();
    Send(advert);
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::StoreResponse(const PrepareMessage<consensus_type> & message)
{
    _cur_prepare.reset(new PrepareMessage<consensus_type>(message));
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::StoreResponse(const CommitMessage<consensus_type> & message)
{
    _cur_commit.reset(new CommitMessage<consensus_type>(message));
}

template<ConsensusType consensus_type>
void ConsensusConnection<consensus_type>::OnPrequel(boost::system::error_code const & ec, const uint8_t *data, size_t size)
{
    std::memcpy(_receive_buffer.data(), data, size);
    OnData(ec, size);
}

template class ConsensusConnection<ConsensusType::BatchStateBlock>;
template class ConsensusConnection<ConsensusType::MicroBlock>;
