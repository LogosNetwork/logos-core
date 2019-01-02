#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/messages/rejection.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/consensus_state.hpp>
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
class MessageParser
{
public:

  virtual ~MessageParser() {}

  // return true iff data is good
  virtual bool OnMessageData(const uint8_t * data,
          uint8_t version,
          MessageType message_type,
          ConsensusType consensus_type,
          uint32_t payload_size) = 0;
};

template<ConsensusType CT>
class ConsensusConnection : public MessageParser
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
    using ApprovedBlock   = PostCommittedBlock<CT>;

public:

    ConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                        PrimaryDelegate & primary,
                        RequestPromoter<CT> & promoter,
                        MessageValidator & validator,
                        const DelegateIdentities & ids,
                        EpochEventsNotifier & events_notifier,
                        PersistenceManager<CT> & persistence_manager);

    void Send(const void * data, size_t size);

    virtual ~ConsensusConnection()
    {
        LOG_DEBUG(_log) << "~ConsensusConnection<" << ConsensusToName(CT) << ">";
    }

    bool OnMessageData(const uint8_t * data,
            uint8_t version,
            MessageType message_type,
            ConsensusType consensus_type,
            uint32_t payload_size) override;

    virtual bool IsPrePrepared(const BlockHash & hash) = 0;

    bool IsRemoteDelegate(uint8_t delegate_id)
    {
        return _delegate_ids.remote == delegate_id;
    }

    virtual void CleanUp() {}

protected:

    static constexpr uint16_t MAX_CLOCK_DRIFT_MS = 20000;

    virtual void ApplyUpdates(const ApprovedBlock &, uint8_t delegate_id) = 0;

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

    //Prepare, Commit, Rejection
    template<typename M>
    void SendMessage(M & msg)
    {
        //        M response(_pre_prepare_hash);
        //        _validator.Sign(response.Hash(), response.signature);

        //        StoreResponse(response);
        //        UpdateMessage(response);

        std::vector<uint8_t> buf;
        msg.Serialize(buf);
        Send(buf.data(), buf.size());
    }

    //    void StoreResponse(const Prepare & message);
    //    void StoreResponse(const Commit & message);
    //    void StoreResponse(const Rejection & message);

    void SetPrePrepare(const PrePrepare & message);
    virtual void HandlePrePrepare(const PrePrepare & message);
    virtual void OnPostCommit();

    //    template<typename M>
    //    void UpdateMessage(M & message);

    virtual void Reject();
    virtual void ResetRejectionStatus();
    virtual void HandleReject(const PrePrepare & message) {}

    virtual bool ValidateReProposal(const PrePrepare & message);


    std::shared_ptr<IOChannel>  _iochannel;
    std::mutex                  _mutex;
    std::shared_ptr<PrePrepare> _pre_prepare;
    //    std::shared_ptr<Prepare>    _prepare; //TODO
    //    std::shared_ptr<Commit>     _commit; //TODO

    uint64_t                    _pre_prepare_timestamp = 0;
    BlockHash                   _prev_pre_prepare_hash;
    AggSignature                _post_prepare_sig;
    AggSignature                _post_commit_sig;
    BlockHash                   _pre_prepare_hash;
    BlockHash                   _post_prepare_hash;
    DelegateIdentities          _delegate_ids;
    RejectionReason             _reason;
    MessageValidator &          _validator;
    Log                         _log;
    PrimaryDelegate &           _primary;
    ConsensusState              _state = ConsensusState::VOID;
    RequestPromoter<CT> &       _promoter; ///< secondary list request promoter
    uint64_t                    _sequence_number = 0;
    EpochEventsNotifier &       _events_notifier;
    PersistenceManager<CT> &    _persistence_manager;
};
