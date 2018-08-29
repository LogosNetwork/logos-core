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

/// ConsensusConnection's Interface to ConsensusNetIO.
class IConsensusConnection
{

public:

  virtual ~IConsensusConnection() {}

  virtual void OnPrequel(boost::system::error_code const & ec,
                         const uint8_t *data, size_t size) = 0;
};

template<ConsensusType consensus_type>
class ConsensusConnection : public IConsensusConnection
{

protected:

    // Boost aliases
    using Service     = boost::asio::io_service;
    using Endpoint    = boost::asio::ip::tcp::endpoint;
    using Socket      = boost::asio::ip::tcp::socket;
    using Log         = boost::log::sources::logger_mt;

    // Message type aliases
    using PrePrepare  = PrePrepareMessage<consensus_type>;
    using Prepare     = PrepareMessage<consensus_type>;
    using Commit      = CommitMessage<consensus_type>;
    using PostPrepare = PostPrepareMessage<consensus_type>;
    using PostCommit  = PostCommitMessage<consensus_type>;

    // Other
    template<MessageType TYPE>
    using SPMessage   = StandardPhaseMessage<TYPE, consensus_type>;

public:

    ConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
                        logos::alarm & alarm,
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

    virtual ~ConsensusConnection() {}

    void OnPrequel(boost::system::error_code const & ec, const uint8_t *data, size_t size);

protected:

    static constexpr uint64_t BUFFER_SIZE = sizeof(PrePrepare);

    using ReceiveBuffer = std::array<uint8_t, BUFFER_SIZE>;

    void ApplyUpdates(const PrePrepare &, uint8_t delegate_id);

    void OnData(boost::system::error_code const & ec, size_t size);
    void OnMessage(boost::system::error_code const & ec, size_t size);

    // Messages received by backup delegates
    void OnConsensusMessage(const PrePrepare & message);
    void OnConsensusMessage(const PostPrepare & message);
    void OnConsensusMessage(const PostCommit & message);

    // Messages received by primary delegates
    template<MessageType Type>
    void OnConsensusMessage(const SPMessage<Type> & message);

    template<typename MSG>
    bool Validate(const MSG & message);
    bool Validate(const PrePrepare & message);

    template<typename MSG>
    bool ProceedWithMessage(const MSG & message, ConsensusState expected_state);
    bool ProceedWithMessage(const PostCommit & message);

    template<typename MSG>
    void SendMessage();

    void SendKeyAdvertisement();

    void StoreResponse(const Prepare & message);
    void StoreResponse(const Commit & message);

    std::shared_ptr<IIOChannel> _iochannel;
    ReceiveBuffer               _receive_buffer;
    std::shared_ptr<PrePrepare> _cur_pre_prepare;
    std::shared_ptr<Prepare>    _cur_prepare;
    std::shared_ptr<Commit>     _cur_commit;
    BlockHash                   _cur_pre_prepare_hash;
    DelegateIdentities          _delegate_ids;
    PersistenceManager &        _persistence_manager;
    DelegateKeyStore &          _key_store;
    MessageValidator &          _validator;
    logos::alarm &              _alarm;
    Log                         _log;
    PrimaryDelegate *           _primary;
    ConsensusState              _state     = ConsensusState::VOID;
    bool                        _connected = false;
};