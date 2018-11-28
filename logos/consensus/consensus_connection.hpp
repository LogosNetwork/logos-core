#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/messages/rejection.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/consensus_state.hpp>
#include <logos/consensus/consensus_p2p.hpp>
#include <logos/node/client_callback.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

class IOChannel;
class EpochEventsNotifier;

struct DelegateIdentities
{
    uint8_t local;
    uint8_t remote;
};

template<ConsensusType CT>
class RequestPromoter;

/// ConsensusConnection's Interface to ConsensusNetIO.
class PrequelParser
{
public:

  virtual ~PrequelParser() {}

  virtual void OnPrequel(const uint8_t * data) = 0;
};

template<ConsensusType CT>
class ConsensusConnection : public PrequelParser
{
protected:

    using PrePrepare  = PrePrepareMessage<CT>;
    using Prepare     = PrepareMessage<CT>;
    using Commit      = CommitMessage<CT>;
    using PostPrepare = PostPrepareMessage<CT>;
    using PostCommit  = PostCommitMessage<CT>;
    using Rejection   = RejectionMessage<CT>;

    template<MessageType T>
    using SPMessage   = StandardPhaseMessage<T, CT>;

public:

    ConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                        PrimaryDelegate & primary,
                        RequestPromoter<CT> & promoter,
                        MessageValidator & validator,
                        const DelegateIdentities & ids,
                        EpochEventsNotifier & events_notifier,
			PersistenceManager<CT> & persistence_manager,
			p2p_interface & p2p);

    void Send(const void * data, size_t size);

    template<typename T>
    void Send(const T & data)
    {
        Send(reinterpret_cast<const void *>(&data), sizeof(data));
    }

    virtual ~ConsensusConnection() {}

    void OnPrequel(const uint8_t * data) override;

    virtual bool IsPrePrepared(const logos::block_hash & hash) = 0;

protected:

    static constexpr uint64_t BUFFER_SIZE        = sizeof(PrePrepare);
    static constexpr uint16_t MAX_CLOCK_DRIFT_MS = 20000;

    using ReceiveBuffer = std::array<uint8_t, BUFFER_SIZE>;

    virtual void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) = 0;

    void OnData();
    void OnMessage(const uint8_t * data);

    // Messages received by backup delegates
    void OnConsensusMessage(const PrePrepare & message);
    void OnConsensusMessage(const PostPrepare & message);
    void OnConsensusMessage(const PostCommit & message);

    // Messages received by primary delegates
    template<typename M>
    void OnConsensusMessage(const M & message);

    template<typename M>
    bool Validate(const M & message);
    bool Validate(const PrePrepare & message);

    template<typename M, typename S>
    bool ValidateSignature(const M & m, const S & s);
    template<typename M>
    bool ValidateSignature(const M & m);

    bool ValidateTimestamp(const PrePrepare & message);

    virtual bool DoValidate(const PrePrepare & message) = 0;
    template<typename M>
    bool ValidateEpoch(const M & m)
    {
        return true;
    }

    template<typename M>
    bool ProceedWithMessage(const M & message, ConsensusState expected_state);
    bool ProceedWithMessage(const PostCommit & message);

    template<typename M>
    void SendMessage()
    {
        M response(_pre_prepare_timestamp);

        response.previous = _pre_prepare_hash;
        _validator.Sign(response);

        StoreResponse(response);
        UpdateMessage(response);

        Send(response);
    }

    void StoreResponse(const Prepare & message);
    void StoreResponse(const Commit & message);
    void StoreResponse(const Rejection & message);

    void SetPrePrepare(const PrePrepare & message);
    virtual void HandlePrePrepare(const PrePrepare & message);
    virtual void OnPostCommit();

    template<typename M>
    void UpdateMessage(M & message);

    virtual void Reject();
    virtual void ResetRejectionStatus();
    virtual void HandleReject(const PrePrepare & message) {}

    virtual bool ValidateReProposal(const PrePrepare & message);


    std::shared_ptr<IOChannel>  _iochannel;
    ReceiveBuffer               _receive_buffer;
    std::mutex                  _mutex;
    std::shared_ptr<PrePrepare> _pre_prepare;
    std::shared_ptr<Prepare>    _prepare;
    std::shared_ptr<Commit>     _commit;
    uint64_t                    _pre_prepare_timestamp = 0;
    BlockHash                   _pre_prepare_hash;
    BlockHash                   _prev_pre_prepare_hash = 0;
    DelegateIdentities          _delegate_ids;
    RejectionReason             _reason;
    MessageValidator &          _validator;
    Log                         _log;
    PrimaryDelegate &           _primary;
    ConsensusState              _state = ConsensusState::VOID;
    RequestPromoter<CT> &       _promoter; ///< secondary list request promoter
    uint64_t                    _sequence_number = 0;
    EpochEventsNotifier &       _events_notifier;
    PersistenceManager<CT> &   _persistence_manager;
    ConsensusP2p<CT>            _consensus_p2p;
};
