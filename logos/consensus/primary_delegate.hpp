#pragma once

#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_state.hpp>
#include <logos/consensus/messages/util.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

class PrimaryDelegate
{

    using Log        = boost::log::sources::logger_mt;
    using Signatures = std::vector<MessageValidator::DelegateSignature>;

public:

    PrimaryDelegate(MessageValidator & validator);

    template<typename MSG>
    void OnConsensusMessage(const MSG & message, uint8_t delegate_id)
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        _cur_delegate_id = delegate_id;
        ProcessMessage(message);
    }

    template<typename MSG>
    bool Validate(const MSG & message);

    virtual ~PrimaryDelegate()
    {}

    template<typename MSG>
    void Send();

    virtual void Send(const void * data, size_t size) = 0;

protected:

    void OnConsensusInitiated(const BatchStateBlock & block);

    // TODO: Revert to std::mutex after
    //       benchmark.
    //
    std::recursive_mutex _mutex;
    ConsensusState       _state = ConsensusState::VOID;

private:

    static constexpr uint8_t QUORUM_SIZE = 7;

    void ProcessMessage(const PrepareMessage & message);
    void ProcessMessage(const CommitMessage & message);

    bool ReachedQuorum();

    template<typename MSG>
    bool ProceedWithMessage(const MSG & message, ConsensusState expected_state);

    void AdvanceState(ConsensusState new_state);

    virtual void OnConsensusReached() = 0;

    Signatures         _signatures;
    Log                _log;
    MessageValidator & _validator;
    BlockHash          _cur_batch_hash;
    uint64_t           _cur_batch_timestamp;
    uint8_t            _cur_delegate_id = 0;
    uint8_t            _consensus_count = 0;
};
