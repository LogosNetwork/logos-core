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

  virtual void OnPrequel(const uint8_t * data) = 0;
};

template<ConsensusType CT>
class ConsensusConnection : public IConsensusConnection
{
protected:

    using Log         = boost::log::sources::logger_mt;
    using PrePrepare  = PrePrepareMessage<CT>;
    using Prepare     = PrepareMessage<CT>;
    using Commit      = CommitMessage<CT>;
    using PostPrepare = PostPrepareMessage<CT>;
    using PostCommit  = PostCommitMessage<CT>;

    template<MessageType TYPE>
    using SPMessage   = StandardPhaseMessage<TYPE, CT>;

public:

    ConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
                        PrimaryDelegate * primary,
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

    void OnPrequel(const uint8_t * data) override;

protected:

    static constexpr uint64_t BUFFER_SIZE = sizeof(PrePrepare);

    using ReceiveBuffer = std::array<uint8_t, BUFFER_SIZE>;

    virtual void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) = 0;

    void OnData();
    void OnMessage(const uint8_t * data);

    // Messages received by backup delegates
    void OnConsensusMessage(const PrePrepare & message);
    void OnConsensusMessage(const PostPrepare & message);
    void OnConsensusMessage(const PostCommit & message);

    // Messages received by primary delegates
    template<MessageType MT>
    void OnConsensusMessage(const SPMessage<MT> & message);

    template<typename MSG>
    bool Validate(const MSG & message);
    virtual bool Validate(const PrePrepare & message) = 0;

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
    DelegateKeyStore &          _key_store;
    MessageValidator &          _validator;
    Log                         _log;
    PrimaryDelegate *           _primary;
    ConsensusState              _state     = ConsensusState::VOID;
    bool                        _connected = false;
};
