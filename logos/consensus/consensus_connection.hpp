#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/consensus_state.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

class IIOChannel;

namespace logos
{
    class alarm;
}

struct DelegateIdentities
{
    uint8_t local;
    uint8_t remote;
};

/// ConsensusConnection's Interface to ConsensusNetIO
class IConsensusConnection {
public:
  IConsensusConnection() {}
  virtual ~IConsensusConnection() {}
  virtual void OnPrequel(boost::system::error_code const & ec, const uint8_t *data, size_t size) = 0;
};

// ConsensusConnection's Interface to ConnectionManager
class IConsensusConnectionOutChannel {
public:
  IConsensusConnectionOutChannel() {}
  virtual ~IConsensusConnectionOutChannel() {}
  virtual void Send(const void *, size_t) = 0;
};

template<ConsensusType consensus_type>
class ConsensusConnection : public IConsensusConnection, public IConsensusConnectionOutChannel
{
protected:

    using Service  = boost::asio::io_service;
    using Endpoint = boost::asio::ip::tcp::endpoint;
    using Socket   = boost::asio::ip::tcp::socket;
    using Log      = boost::log::sources::logger_mt;

public:

    ConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
	                    logos::alarm & alarm,
                        PrimaryDelegate * primary,
	                    PersistenceManager & persistence_manager,
                        DelegateKeyStore & key_store,
	                    MessageValidator & validator,
	                    const DelegateIdentities & ids);

	virtual void Send(const void * data, size_t size) override;

    template<typename TYPE>
    void Send(const TYPE & data)
    {
        Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }

    virtual ~ConsensusConnection() {}

    void OnPrequel(boost::system::error_code const & ec, const uint8_t *data, size_t size);

protected:

	static constexpr uint64_t BUFFER_SIZE         = sizeof(PrePrepareMessage<consensus_type>);

    using ReceiveBuffer = std::array<uint8_t, BUFFER_SIZE>;

    virtual void ApplyUpdates(const PrePrepareMessage<consensus_type> &, uint8_t delegate_id) = 0;

    void OnData(boost::system::error_code const & ec, size_t size);
    void OnMessage(boost::system::error_code const & ec, size_t size);

    // Messages received by backup delegates
    void OnConsensusMessage(const PrePrepareMessage<consensus_type> & message);
    void OnConsensusMessage(const PostPrepareMessage<consensus_type> & message);
    void OnConsensusMessage(const PostCommitMessage<consensus_type> & message);

    // Messages received by primary delegates
    template<MessageType Type>
    void OnConsensusMessage(const StandardPhaseMessage<Type, consensus_type> & message);

    template<typename MSG>
    bool Validate(const MSG & message);
    virtual bool Validate(const PrePrepareMessage<consensus_type> & message) = 0;

    template<typename MSG>
    bool ProceedWithMessage(const MSG & message, ConsensusState expected_state);
    bool ProceedWithMessage(const PostCommitMessage<consensus_type> & message);

    template<typename MSG>
    void SendMessage();

    void SendKeyAdvertisement();

    void StoreResponse(const PrepareMessage<consensus_type> & message);
    void StoreResponse(const CommitMessage<consensus_type> & message);

    std::shared_ptr<IIOChannel>        _iochannel;
    ReceiveBuffer                      _receive_buffer;
    std::shared_ptr<PrePrepareMessage<consensus_type>> _cur_pre_prepare;
    std::shared_ptr<PrepareMessage<consensus_type>>    _cur_prepare;
    std::shared_ptr<CommitMessage<consensus_type>>     _cur_commit;
    BlockHash                          _cur_pre_prepare_hash;
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

template<ConsensusType consensus_type, typename Type = void>
struct ConsensusConnectionT;
