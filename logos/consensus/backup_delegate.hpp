#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/messages/rejection.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/message_handler.hpp>
#include <logos/consensus/consensus_state.hpp>
#include <logos/consensus/p2p/consensus_p2p.hpp>
#include <logos/consensus/delegate_bridge.hpp>
#include <logos/node/client_callback.hpp>
#include <logos/lib/utility.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

class IOChannel;
class EpochEventsNotifier;
class ConsensusScheduler;

struct DelegateIdentities
{
    uint8_t local;
    uint8_t remote;
};

template<ConsensusType CT>
class BackupDelegate : public DelegateBridge<CT>,
                       public Self<BackupDelegate<CT>>
{
protected:

    using PrePrepare    = PrePrepareMessage<CT>;
    using Prepare       = PrepareMessage<CT>;
    using Commit        = CommitMessage<CT>;
    using PostPrepare   = PostPrepareMessage<CT>;
    using PostCommit    = PostCommitMessage<CT>;
    using Rejection     = RejectionMessage<CT>;
    using ApprovedBlock = PostCommittedBlock<CT>;
    using Service       = boost::asio::io_service;
    using Store         = logos::block_store;
    using Cache         = logos::IBlockCache;

    template<MessageType T>
    using SPMessage = StandardPhaseMessage<T, CT>;
    template<typename T>
    using WPTR          = std::weak_ptr<T>;

public:

    BackupDelegate(std::shared_ptr<IOChannel> iochannel,
                   std::shared_ptr<PrimaryDelegate> primary,
                   Store & store,
                   Cache & block_cache,
                   MessageValidator & validator,
                   const DelegateIdentities & ids,
                   ConsensusScheduler & scheduler,
                   std::shared_ptr<EpochEventsNotifier> events_notifier,
                   PersistenceManager<CT> & persistence_manager,
                   p2p_interface & p2p,
                   Service & service);

    virtual ~BackupDelegate()
    {
        LOG_DEBUG(_log) << "~BackupDelegate<" << ConsensusToName(CT) << ">";
    }

    bool IsRemoteDelegate(uint8_t delegate_id)
    {
        return _delegate_ids.remote == delegate_id;
    }

    /// set previous hash, microblock and epoch block have only one chain
    /// consequently in the override function have to set all backup's hash to previous
    /// @param hash to set
    virtual void SetPreviousPrePrepareHash(const BlockHash &hash)
    {
        _prev_pre_prepare_hash = hash;
    }

    uint8_t GetDelegateId()
    {
        return _delegate_ids.local;
    }

    uint8_t RemoteDelegateId()
    {
        return _delegate_ids.remote;
    }

protected:

    static constexpr uint16_t MAX_CLOCK_DRIFT_MS = 20000;

    virtual MessageHandler<CT> & GetHandler() = 0;
    virtual void ApplyUpdates(const ApprovedBlock &, uint8_t delegate_id) = 0;

    // Messages received by backup delegates
    void OnConsensusMessage(const PrePrepare & message) override;
    void OnConsensusMessage(const PostPrepare & message) override;
    void OnConsensusMessage(const PostCommit & message) override;
    void OnPostCommittedBlock(const ApprovedBlock & block) override;

    // Messages received by primary delegates
    void OnConsensusMessage(const Prepare & message) override;
    void OnConsensusMessage(const Commit & message) override;
    void OnConsensusMessage(const Rejection & message) override;


    template<typename M>
    bool Validate(const M & message);
    bool Validate(const PrePrepare & message);

    template<typename M>
    bool ValidateQuorum(const M & message);

    virtual bool ValidateTimestamp(const PrePrepare & message);

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
        std::vector<uint8_t> buf;
        msg.Serialize(buf);
        this->Send(buf.data(), buf.size());
        this->SendP2p(buf.data(), buf.size(), msg.type, _epoch_number, _delegate_ids.remote);
    }

    void SetPrePrepare(const PrePrepare & message);
    virtual void HandlePrePrepare(const PrePrepare & message);
    void OnPostCommit();
    bool IsOldBlock(const PrePrepare &);
    virtual void AdvanceCounter(){}

    virtual void Reject(const BlockHash &);
    virtual void ResetRejectionStatus();
    virtual void HandleReject(const PrePrepare & message) {}

    virtual bool ValidateReProposal(const PrePrepare & message);

    std::mutex                  _mutex;
    std::shared_ptr<PrePrepare> _pre_prepare;
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
    WPTR<PrimaryDelegate>       _primary;
    Store &                     _store;
    Cache &                     _block_cache;
    ConsensusState              _state = ConsensusState::VOID;
    ConsensusScheduler &        _scheduler;
    uint64_t                    _sequence_number = 0;
    WPTR<EpochEventsNotifier>   _events_notifier;
    PersistenceManager<CT> &    _persistence_manager;
    uint32_t                    _epoch_number;
    uint32_t                    _expected_epoch_number;
    std::mutex                  _post_commit_mutex;
};
