#pragma once

#include <rai/consensus/persistence/persistence_manager.hpp>
#include <rai/consensus/message_validator.hpp>
#include <rai/consensus/messages/messages.hpp>
#include <rai/consensus/primary_delegate.hpp>
#include <rai/consensus/consensus_state.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

namespace rai
{
    class alarm;
}

class ConsensusConnection
{

    using Service  = boost::asio::io_service;
    using Endpoint = boost::asio::ip::tcp::endpoint;
    using Socket   = boost::asio::ip::tcp::socket;
    using Log      = boost::log::sources::logger_mt;

public:

    // TODO: Consolidate constructors
    //
	ConsensusConnection(Service & service,
	                    rai::alarm & alarm,
	                    Log & log,
	                    const Endpoint & endpoint,
	                    PrimaryDelegate * primary,
	                    PersistenceManager & persistence_manager,
	                    MessageValidator & validator);

	ConsensusConnection(std::shared_ptr<Socket> socket,
                        rai::alarm & alarm,
                        Log & log,
                        const Endpoint & endpoint,
                        PrimaryDelegate * primary,
                        PersistenceManager & persistence_manager,
                        MessageValidator & validator);

	void Send(const void * data, size_t size);

    template<typename TYPE>
    void Send(const TYPE & data)
    {
        Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }

private:

    static constexpr uint8_t  CONNECT_RETRY_DELAY = 5;
	static constexpr uint16_t BUFFER_SIZE         = 32768;

    using ReceiveBuffer = std::array<uint8_t, BUFFER_SIZE>;

    void Connect();
    void Read();

    void OnConnect();
    void OnConnect(boost::system::error_code const & ec);
    void OnData(boost::system::error_code const & ec, size_t size);
    void OnMessage(boost::system::error_code const & ec, size_t size);

    // Messages received by backup delegates
    void OnConsensusMessage(const PrePrepareMessage & message);
    void OnConsensusMessage(const PostPrepareMessage & message);
    void OnConsensusMessage(const PostCommitMessage & message);

    // Messages received by primary delegates
    template<MessageType Type>
    void OnConsensusMessage(const StandardPhaseMessage<Type> & message);

    template<typename MSG>
    bool Validate(const MSG & message);

    template<typename MSG>
    bool ProceedWithMessage(const MSG & message, ConsensusState expected_state);

    template<typename MSG>
    void SendMessage();

    void SendKeyAdvertisement();

    ReceiveBuffer                      _receive_buffer;
    std::shared_ptr<Socket>            _socket;
    Endpoint                           _endpoint;
    std::shared_ptr<PrePrepareMessage> _cur_batch;
    PersistenceManager &               _persistence_manager;
    MessageValidator &                 _validator;
    rai::alarm &                       _alarm;
    Log &                              _log;
    PrimaryDelegate *                  _primary;
    ConsensusState                     _state     = ConsensusState::VOID;
    bool                               _connected = false;
};
