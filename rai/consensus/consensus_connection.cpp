#include <rai/consensus/consensus_connection.hpp>

#include <rai/node/node.hpp>

#include <boost/asio/read.hpp>

const uint8_t ConsensusConnection::CONNECT_RETRY_DELAY;

ConsensusConnection::ConsensusConnection(boost::asio::io_service & service,
                                         rai::alarm & alarm,
                                         Log & log,
                                         const Endpoint & endpoint,
                                         PrimaryDelegate * primary)
    : socket_(new Socket(service))
    , endpoint_(endpoint)
    , log_(log)
    , primary_(primary)
    , alarm_(alarm)
{
    BOOST_LOG(log_) << "ConsensusConnection - Trying to connect to: " << endpoint_;
    Connect();
}

ConsensusConnection::ConsensusConnection(std::shared_ptr<Socket> socket,
                                         rai::alarm & alarm,
                                         Log & log,
                                         const Endpoint & endpoint,
                                         PrimaryDelegate * primary)
    : socket_(socket)
    , endpoint_(endpoint)
    , alarm_(alarm)
    , log_(log)
    , primary_(primary)
    , connected_(true)
{
    Read();
}

void ConsensusConnection::Send(const void * data, size_t size)
{
    auto send_buffer (std::make_shared<std::vector<uint8_t>>(size, uint8_t(0)));
    std::memcpy(send_buffer->data(), data, size);

    boost::asio::async_write(*socket_, boost::asio::buffer(send_buffer->data(),
                                                           send_buffer->size()),
                             [send_buffer, this](boost::system::error_code const & ec, size_t size_a)
                             {
                                  if(ec)
                                  {
                                      BOOST_LOG(log_) << "ConsensusConnection - Error on write to socket: "
                                                      << ec.message();
                                  }
                             });
}

void ConsensusConnection::Connect()
{
    socket_->async_connect(endpoint_,
                           std::bind(&ConsensusConnection::OnConnect, this,
                                     std::placeholders::_1));
}

void ConsensusConnection::Read()
{
    boost::asio::async_read(*socket_, boost::asio::buffer(receive_buffer_.data(),
                                                          sizeof(Prequel)),
                            std::bind(&ConsensusConnection::OnData, this,
                                      std::placeholders::_1,
                                      std::placeholders::_2));
}

void ConsensusConnection::OnConnect(boost::system::error_code const & ec)
{
    if(!ec)
    {
        BOOST_LOG(log_) << "ConsensusConnection - Connected to " << endpoint_;
        connected_ = true;

        Read();
    }
    else
    {
        BOOST_LOG(log_) << "ConsensusConnection - Error connecting to " << endpoint_ << " : " << ec.message()
                        << " Retrying in " << int(CONNECT_RETRY_DELAY) << " seconds.";

        socket_->close();

        alarm_.add(std::chrono::steady_clock::now() + std::chrono::seconds(CONNECT_RETRY_DELAY),
                   std::bind(&ConsensusConnection::Connect, this));
    }
}

void ConsensusConnection::OnData(boost::system::error_code const & ec, size_t size)
{
    if(size != sizeof(Prequel))
    {
        BOOST_LOG(log_) << "ConsensusConnection - Error, only received " << size << " bytes";
        return;
    }

    if(ec)
    {
        BOOST_LOG(log_) << "ConsensusConnection - Error receiving message prequel: " << ec.message();
        return;
    }

    MessageType type (static_cast<MessageType> (receive_buffer_.data()[1]));
    switch (type)
    {
        case MessageType::Pre_Prepare:
            boost::asio::async_read(*socket_, boost::asio::buffer(receive_buffer_.data() + sizeof(Prequel),
                                                                  sizeof(PrePrepareMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Prepare:
            boost::asio::async_read(*socket_, boost::asio::buffer(receive_buffer_.data() + sizeof(Prequel),
                                                                  sizeof(PrepareMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Post_Prepare:
            boost::asio::async_read(*socket_, boost::asio::buffer(receive_buffer_.data() + sizeof(Prequel),
                                                                  sizeof(PostPrepareMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Commit:
            boost::asio::async_read(*socket_, boost::asio::buffer(receive_buffer_.data() + sizeof(Prequel),
                                                                  sizeof(CommitMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Post_Commit:
            boost::asio::async_read(*socket_, boost::asio::buffer(receive_buffer_.data() + sizeof(Prequel),
                                                                  sizeof(PostCommitMessage) -
                                                                  sizeof(Prequel)
                                                                  ),
                                    std::bind(&ConsensusConnection::OnMessage, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
            break;
        case MessageType::Unknown:
            BOOST_LOG(log_) << "ConsensusConnection - Received unknown message type";
            break;
        default:
            break;
    }
}

void ConsensusConnection::OnMessage(boost::system::error_code const & ec, size_t size)
{
    if(ec)
    {
        BOOST_LOG(log_) << "ConsensusConnection - Error receiving message: " << ec.message();
        return;
    }

    MessageType type (static_cast<MessageType> (receive_buffer_.data()[1]));
    switch (type)
    {
        case MessageType::Pre_Prepare: {
            BOOST_LOG(log_) << "ConsensusConnection - Received pre prepare message";
            auto msg (*reinterpret_cast<PrePrepareMessage*>(receive_buffer_.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Prepare: {
            BOOST_LOG(log_) << "ConsensusConnection - Received prepare message";
            auto msg (*reinterpret_cast<PrepareMessage*>(receive_buffer_.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Prepare: {
            BOOST_LOG(log_) << "ConsensusConnection - Received post prepare message";
            auto msg (*reinterpret_cast<PostPrepareMessage*>(receive_buffer_.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Commit: {
            BOOST_LOG(log_) << "ConsensusConnection - Received commit message";
            auto msg (*reinterpret_cast<CommitMessage*>(receive_buffer_.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Post_Commit: {
            BOOST_LOG(log_) << "ConsensusConnection - Received post commit message";
            auto msg (*reinterpret_cast<PostCommitMessage*>(receive_buffer_.data()));
            OnConsensusMessage(msg);
            break;
        }
        case MessageType::Unknown:
            BOOST_LOG(log_) << "ConsensusConnection - Received unknown message type";
            break;
    }

    Read();
}

void ConsensusConnection::OnConsensusMessage(const PrePrepareMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::VOID))
    {
        state_ = ConsensusState::PREPARE;

        PrepareMessage response;
        Send(response);
    }
}

void ConsensusConnection::OnConsensusMessage(const PrepareMessage & message)
{
    if(Validate(message))
    {
        primary_->OnConsensusMessage(message);
    }
}

void ConsensusConnection::OnConsensusMessage(const PostPrepareMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::PREPARE))
    {
        state_ = ConsensusState::COMMIT;

        CommitMessage response;
        Send(response);
    }
}

void ConsensusConnection::OnConsensusMessage(const CommitMessage & message)
{
    if(Validate(message))
    {
        primary_->OnConsensusMessage(message);
    }
}

void ConsensusConnection::OnConsensusMessage(const PostCommitMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::COMMIT))
    {
        state_ = ConsensusState::VOID;
    }
}

template<typename Msg>
bool ConsensusConnection::Validate(const Msg & msg)
{
    return true;
}

template<typename MSG>
bool ConsensusConnection::ProceedWithMessage(const MSG & message, ConsensusState expected_state)
{
    if(state_ != expected_state)
    {
        BOOST_LOG(log_) << "ConsensusConnection - Error! Received PostCommitMessage while in "
                        << StateToString(state_);

        return false;
    }

    if(Validate(message))
    {
        return true;
    }

    return false;
}

