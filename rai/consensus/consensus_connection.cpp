#include <rai/consensus/consensus_connection.hpp>

#include <rai/node/node.hpp>

#include <boost/asio/read.hpp>

const uint8_t ConsensusConnection::CONNECT_RETRY_DELAY;

ConsensusConnection::ConsensusConnection(Service & service,
                                         rai::alarm & alarm,
                                         const Endpoint & endpoint,
                                         PrimaryDelegate * primary,
                                         PersistenceManager & persistence_manager,
                                         MessageValidator & validator,
                                         const DelegateIdentities & ids)
    : _socket(new Socket(service))
    , _endpoint(endpoint)
    , _delegate_ids(ids)
    , _persistence_manager(persistence_manager)
    , _validator(validator)
    , _primary(primary)
    , _alarm(alarm)
{
    BOOST_LOG(_log) << "ConsensusConnection - Trying to connect to: " << _endpoint;
    Connect();
}

ConsensusConnection::ConsensusConnection(std::shared_ptr<Socket> socket,
                                         rai::alarm & alarm,
                                         const Endpoint & endpoint,
                                         PrimaryDelegate * primary,
                                         PersistenceManager & persistence_manager,
                                         MessageValidator & validator,
                                         const DelegateIdentities & ids)
    : _socket(socket)
    , _endpoint(endpoint)
    , _delegate_ids(ids)
    , _persistence_manager(persistence_manager)
    , _validator(validator)
    , _alarm(alarm)
    , _primary(primary)
{
    OnConnect();
}

void ConsensusConnection::Send(const void * data, size_t size)
{
    auto send_buffer (std::make_shared<std::vector<uint8_t>>(size, uint8_t(0)));
    std::memcpy(send_buffer->data(), data, size);

    boost::asio::async_write(*_socket, boost::asio::buffer(send_buffer->data(),
                                                           send_buffer->size()),
                             [send_buffer, this](boost::system::error_code const & ec, size_t size_a)
                             {
                                  if(ec)
                                  {
                                      BOOST_LOG(_log) << "ConsensusConnection - Error on write to socket: "
                                                      << ec.message();
                                  }
                             });
}

void ConsensusConnection::Connect()
{
    _socket->async_connect(_endpoint,
                           [this](boost::system::error_code const & ec) { OnConnect(ec); });
}

void ConsensusConnection::Read()
{
    boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data(),
                                                          sizeof(Prequel)),
                            std::bind(&ConsensusConnection::OnData, this,
                                      std::placeholders::_1,
                                      std::placeholders::_2));
}

void ConsensusConnection::OnConnect()
{
    BOOST_LOG(_log) << "ConsensusConnection - Connected to "
                    << _endpoint << ". Remote delegate id: "
                    << uint64_t(_delegate_ids.remote);

    _connected = true;

    SendKeyAdvertisement();
    Read();
}

void ConsensusConnection::OnConnect(boost::system::error_code const & ec)
{
    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error connecting to "
                        << _endpoint << " : " << ec.message()
                        << " Retrying in " << int(CONNECT_RETRY_DELAY)
                        << " seconds.";

        _socket->close();

        _alarm.add(std::chrono::seconds(CONNECT_RETRY_DELAY),
                   std::bind(&ConsensusConnection::Connect, this));

        return;
    }

    OnConnect();
}

void ConsensusConnection::OnData(boost::system::error_code const & ec, size_t size)
{
    if(size != sizeof(Prequel))
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error, only received " << size << " bytes";
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
            boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(PrePrepareMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Prepare:
            boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(PrepareMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Post_Prepare:
            boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(PostPrepareMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Commit:
            boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(CommitMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Post_Commit:
            boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(PostCommitMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Key_Advert:
            boost::asio::async_read(*_socket, boost::asio::buffer(_receive_buffer.data() + sizeof(Prequel),
                                                                  sizeof(KeyAdvertisement) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
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

void ConsensusConnection::OnMessage(boost::system::error_code const & ec, size_t size)
{
    if(ec)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error receiving message: " << ec.message();
        return;
    }

    MessageType type (static_cast<MessageType> (_receive_buffer.data()[1]));
    switch (type)
    {
        case MessageType::Pre_Prepare: {
            BOOST_LOG(_log) << "ConsensusConnection - Received pre prepare message";
            auto msg (*reinterpret_cast<PrePrepareMessage*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Prepare: {
            BOOST_LOG(_log) << "ConsensusConnection - Received prepare message";
            auto msg (*reinterpret_cast<PrepareMessage*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Prepare: {
            BOOST_LOG(_log) << "ConsensusConnection - Received post prepare message";
            auto msg (*reinterpret_cast<PostPrepareMessage*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Commit: {
            BOOST_LOG(_log) << "ConsensusConnection - Received commit message";
            auto msg (*reinterpret_cast<CommitMessage*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Commit: {
            BOOST_LOG(_log) << "ConsensusConnection - Received post commit message";
            auto msg (*reinterpret_cast<PostCommitMessage*>(_receive_buffer.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Key_Advert: {
            BOOST_LOG(_log) << "ConsensusConnection - Received key advertisement";
            auto msg (*reinterpret_cast<KeyAdvertisement*>(_receive_buffer.data()));
            _validator.OnPublicKey(_delegate_ids.remote, msg.public_key);
            break;
        }
        case MessageType::Unknown:
            BOOST_LOG(_log) << "ConsensusConnection - Received unknown message type";
            break;
    }

    Read();
}

void ConsensusConnection::OnConsensusMessage(const PrePrepareMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::VOID))
    {
        _state = ConsensusState::PREPARE;
        _cur_batch.reset(new PrePrepareMessage(message));
        _cur_batch_hash = message.Hash();

        SendMessage<PrepareMessage>();
    }
}

void ConsensusConnection::OnConsensusMessage(const PostPrepareMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::PREPARE))
    {
        _state = ConsensusState::COMMIT;

        SendMessage<CommitMessage>();
    }
}

void ConsensusConnection::OnConsensusMessage(const PostCommitMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::COMMIT))
    {
        assert(_cur_batch);

        _persistence_manager.ApplyUpdates(*_cur_batch, _delegate_ids.remote);
        _state = ConsensusState::VOID;
    }
}

template<MessageType Type>
void ConsensusConnection::OnConsensusMessage(const StandardPhaseMessage<Type> & message)
{
    _primary->OnConsensusMessage(message, _delegate_ids.remote);
}

template<typename MSG>
bool ConsensusConnection::Validate(const MSG & message)
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

template<>
bool ConsensusConnection::Validate<PrePrepareMessage>(const PrePrepareMessage & message)
{
    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        if(!_persistence_manager.Validate(message.blocks[i], _delegate_ids.remote))
        {
            return false;
        }
    }

    return true;
}

template<typename MSG>
bool ConsensusConnection::ProceedWithMessage(const MSG & message, ConsensusState expected_state)
{
    if(_state != expected_state)
    {
        BOOST_LOG(_log) << "ConsensusConnection - Error! Received " << MessageToName(message)
                        << " message while in " << StateToString(_state);

        return false;
    }

    if(Validate(message))
    {
        return true;
    }

    return false;
}

template<typename MSG>
void ConsensusConnection::SendMessage()
{
    MSG response(_cur_batch->timestamp);

    response.hash = _cur_batch_hash;
    _validator.Sign(response);

    StoreResponse(response);

    Send(response);
}

void ConsensusConnection::SendKeyAdvertisement()
{
    KeyAdvertisement advert;
    advert.public_key = _validator.GetPublicKey();
    Send(advert);
}

void ConsensusConnection::StoreResponse(const PrepareMessage & message)
{
    _cur_prepare.reset(new PrepareMessage(message));
}

void ConsensusConnection::StoreResponse(const CommitMessage & message)
{
    _cur_commit.reset(new CommitMessage(message));
}
