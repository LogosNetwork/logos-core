#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/consensus_state.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

namespace logos
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

    struct DelegateIdentities
    {
        uint8_t local;
        uint8_t remote;
    };

    // TODO: Consolidate constructors
    //
	ConsensusConnection(Service & service,
	                    logos::alarm & alarm,
	                    const Endpoint & endpoint,
	                    PrimaryDelegate * primary,
	                    PersistenceManager & persistence_manager,
	                    DelegateKeyStore & key_store,
	                    MessageValidator & validator,
	                    const DelegateIdentities & ids);

	ConsensusConnection(std::shared_ptr<Socket> socket,
                        logos::alarm & alarm,
                        const Endpoint & endpoint,
                        PrimaryDelegate * primary,
                        PersistenceManager & persistence_manager,
                        DelegateKeyStore & key_store,
                        MessageValidator & validator,
                        const DelegateIdentities & ids);

	void Send(const void * data, size_t size);

    template<typename TYPE>
    void Send(const TYPE & data)
    {
        Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }


private:

    static constexpr uint8_t  CONNECT_RETRY_DELAY = 5;
	static constexpr uint64_t BUFFER_SIZE         = sizeof(PrePrepareMessage);

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
    bool ProceedWithMessage(const PostCommitMessage & message);

    template<typename MSG>
    void SendMessage();

    void SendKeyAdvertisement();

    void StoreResponse(const PrepareMessage & message);
    void StoreResponse(const CommitMessage & message);

    void AdjustSocket();

    ReceiveBuffer                      _receive_buffer;
    std::shared_ptr<Socket>            _socket;
    Endpoint                           _endpoint;
    std::shared_ptr<PrePrepareMessage> _cur_batch;
    std::shared_ptr<PrepareMessage>    _cur_prepare;
    std::shared_ptr<CommitMessage>     _cur_commit;
    BlockHash                          _cur_batch_hash;
    DelegateIdentities                 _delegate_ids;
    PersistenceManager &               _persistence_manager;
    DelegateKeyStore &                 _key_store;
    MessageValidator &                 _validator;
    logos::alarm &                       _alarm;
    Log                                _log;
    PrimaryDelegate *                  _primary;
    ConsensusState                     _state     = ConsensusState::VOID;
    bool                               _connected = false;
};
