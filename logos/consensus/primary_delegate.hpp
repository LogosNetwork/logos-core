#pragma once

#include <logos/consensus/messages/rejection.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_state.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>

class PrimaryDelegate
{
    friend class Archiver;

    using Signatures = std::vector<MessageValidator::DelegateSignature>;
    using Timer      = boost::asio::deadline_timer;
    using Error      = boost::system::error_code;
    using Service    = boost::asio::io_service;
    using Seconds    = boost::posix_time::seconds;

public:

    PrimaryDelegate(Service & service,
                    MessageValidator & validator);

    template<typename M>
    void OnConsensusMessage(const M & message, uint8_t delegate_id)
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        _cur_delegate_id = delegate_id;
        ProcessMessage(message);
    }

    template<typename M>
    bool Validate(const M & message);

    virtual ~PrimaryDelegate()
    {}

    template<typename M>
    void Send(bool propagate = false);

    virtual void Send(const void * data, size_t size, bool propagate = false) = 0;

protected:

    static constexpr uint8_t QUORUM_SIZE = NUM_DELEGATES - 1;

    virtual void UpdateVotes();

    template<ConsensusType C>
    void OnConsensusInitiated(const PrePrepareMessage<C> & block);

    bool StateReadyForConsensus();
    void CancelTimer();

    // TODO: Revert to std::mutex after
    //       benchmark.
    //
    std::recursive_mutex _mutex;
    BlockHash            _prev_hash = 0;
    BlockHash            _cur_hash  = 0;
    ConsensusState       _state           = ConsensusState::VOID;
    uint64_t             _prepare_weight  = 0;
    uint8_t              _cur_delegate_id = 0;

private:

    static const Seconds PRIMARY_TIMEOUT;
    static const Seconds RECALL_TIMEOUT;

    template<ConsensusType C>
    void ProcessMessage(const RejectionMessage<C> & message);
    template<ConsensusType C>
    void ProcessMessage(const PrepareMessage<C> & message);
    template<ConsensusType C>
    void ProcessMessage(const CommitMessage<C> & message);

    virtual void OnRejection(const RejectionMessage<ConsensusType::BatchStateBlock> & message);
    virtual void OnRejection(const RejectionMessage<ConsensusType::MicroBlock> & message);
    virtual void OnRejection(const RejectionMessage<ConsensusType::Epoch> & message);

    void CheckRejection();

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
    bool ProceedWithMessage(const M & message, ConsensusState expected_state);

    void AdvanceState(ConsensusState new_state);
    virtual void OnStateAdvanced();
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
