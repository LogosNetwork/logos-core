#pragma once

#include <logos/consensus/messages/rejection.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_state.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>

#include <unordered_map>

class PrimaryDelegate
{
    using uint128_t = logos::uint128_t;

    struct Weight
    {
        uint128_t vote_weight  = 0;
        uint128_t stake_weight = 0;
    };

    friend class Archiver;

    using Signatures = std::vector<MessageValidator::DelegateSignature>;
    using Timer      = boost::asio::deadline_timer;
    using Error      = boost::system::error_code;
    using Service    = boost::asio::io_service;
    using Seconds    = boost::posix_time::seconds;
    using Store      = logos::block_store;
    using Weights    = std::unordered_map<uint8_t, Weight>;

public:

    enum class ProceedAction : uint8_t
    {
        APPROVED = 0,
        REJECTED,
        DO_NOTHING
    };

    PrimaryDelegate(Service & service,
                    MessageValidator & validator);

    template<typename M>
    void OnConsensusMessage(const M & message, uint8_t remote_delegate_id)
    {
        // SYL Integration: got rid of coarse locking and implemented granular locking inside ProceedWithMessage
        // Changed method signatures to avoid having to keep track of remote delegate id
        ProcessMessage(message, remote_delegate_id);
    }

    template<typename M>
    bool Validate(const M & message, uint8_t remote_delegate_id);

    virtual ~PrimaryDelegate()
    {}

    virtual void OnCurrentEpochSet();

    virtual void Send(const void * data, size_t size) = 0;

    void AdvanceState(ConsensusState new_state);

    template<typename TYPE>
    void Send(const TYPE & data)
    {
        std::vector<uint8_t> buf;
        data.Serialize(buf);
        Send(buf.data(), buf.size());
    }

protected:

    virtual void UpdateVotes();

    template<ConsensusType C>
    void OnConsensusInitiated(const PrePrepareMessage<C> & block);

    bool StateReadyForConsensus();
    void CancelTimer();

    void SetQuorum(uint128_t & max_fault, uint128_t & quorum, const uint128_t & total);
    bool ReachedQuorum(uint128_t vote, uint128_t stake);

    // TODO: Revert to std::mutex after
    //       benchmark.
    //
    std::recursive_mutex _mutex;
    BlockHash            _prev_pre_prepare_hash;
    BlockHash            _pre_prepare_hash;
    BlockHash            _post_prepare_hash;
    DelegateSig          _pre_prepare_sig;
    AggSignature         _post_prepare_sig;
    AggSignature         _post_commit_sig;
    bool                 _ongoing         = false;
    std::mutex           _ongoing_mutex;
    bool                 _state_changing  = false;
    std::mutex           _state_mutex;
    Weights              _weights;
    ApprovedEB           _current_epoch;
    ConsensusState       _state           = ConsensusState::VOID;
    uint128_t            _vote_total      = 0;
    uint128_t            _stake_total     = 0;
    uint128_t            _vote_max_fault  = 0;
    uint128_t            _stake_max_fault = 0;
    uint128_t            _vote_quorum     = 0;
    uint128_t            _stake_quorum    = 0;
    uint128_t            _prepare_vote    = 0;
    uint128_t            _prepare_stake   = 0;
    uint128_t            _my_vote         = 0;
    uint128_t            _my_stake        = 0;
    uint8_t              _delegate_id     = 0;

private:

    static const Seconds PRIMARY_TIMEOUT;
    static const Seconds RECALL_TIMEOUT;

    template<ConsensusType C>
    void ProcessMessage(const RejectionMessage<C> & message, uint8_t remote_delegate_id);
    template<ConsensusType C>
    void ProcessMessage(const PrepareMessage<C> & message, uint8_t remote_delegate_id);
    template<ConsensusType C>
    void ProcessMessage(const CommitMessage<C> & message, uint8_t remote_delegate_id);

    // The Tally method needs to be called with _state_mutex locked to ensure atomicity
    template<ConsensusType C>
    void Tally(const RejectionMessage<C> & message, uint8_t remote_delegate_id);

    template<ConsensusType C>
    void Tally(const PrepareMessage<C> & message, uint8_t remote_delegate_id);

    template<ConsensusType C>
    void Tally(const CommitMessage<C> & message, uint8_t remote_delegate_id);

    template<typename M>
    void TallyStandardPhaseMessage(const M & message, uint8_t remote_delegate_id);

    virtual void TallyPrepareMessage(const PrepareMessage<ConsensusType::BatchStateBlock> & message, uint8_t remote_delegate_id);
    virtual void TallyPrepareMessage(const PrepareMessage<ConsensusType::MicroBlock> & message, uint8_t remote_delegate_id);
    virtual void TallyPrepareMessage(const PrepareMessage<ConsensusType::Epoch> & message, uint8_t remote_delegate_id);

    template<ConsensusType C>
    BlockHash GetHashSigned(const RejectionMessage<C> & message)
    {
        return message.Hash();
    }
    template<ConsensusType C>
    BlockHash GetHashSigned(const PrepareMessage<C> & message)
    {
        return _pre_prepare_hash;
    }
    template<ConsensusType C>
    BlockHash GetHashSigned(const CommitMessage<C> & message)
    {
        return _post_prepare_hash;
    }

    virtual void OnRejection(const RejectionMessage<ConsensusType::BatchStateBlock> & message, uint8_t remote_delegate_id);
    virtual void OnRejection(const RejectionMessage<ConsensusType::MicroBlock> & message, uint8_t remote_delegate_id);
    virtual void OnRejection(const RejectionMessage<ConsensusType::Epoch> & message, uint8_t remote_delegate_id);

    template<ConsensusType C>
    void OnPrePrepareTimeout(const Error & error);
    template<ConsensusType C>
    void OnPostPrepareTimeout(const Error & error);

    template<ConsensusType C>
    void OnTimeout(const Error & error,
                   const std::string & timeout,
                   ConsensusState expected_state);

    template<ConsensusType C>
    void CycleTimers(bool cancel = false);

    bool ReachedQuorum();
    bool AllDelegatesResponded();

    template<typename M>
    ProceedAction ProceedWithMessage(const M & message, ConsensusState expected_state, uint8_t remote_delegate_id);

    virtual void OnStateAdvanced();
    virtual bool IsPrePrepareRejected();
    virtual void OnPrePrepareRejected();

    virtual void OnConsensusReached() = 0;

    Signatures         _signatures;
    Log                _log;
    MessageValidator & _validator;
    Timer              _recall_timer;
    Timer              _primary_timer;
    uint64_t           _cur_batch_timestamp = 0;
    uint8_t            _delegates_responded = 0;
    bool               _timer_cancelled     = false;
};
