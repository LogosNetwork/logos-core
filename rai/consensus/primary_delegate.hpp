#pragma once

#include <rai/consensus/messages/messages.hpp>
#include <rai/consensus/consensus_state.hpp>
#include <rai/consensus/messages/util.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

class PrimaryDelegate
{

    using Log = boost::log::sources::logger_mt;

public:

    PrimaryDelegate(Log & log);

    template<typename MSG>
    void OnConsensusMessage(const MSG & message)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ProcessMessage(message);
    }

    template<typename MSG>
    bool Validate(const MSG & msg);

    virtual ~PrimaryDelegate()
    {}

    virtual void Send(void * data, size_t size) = 0;

protected:

    std::mutex     _mutex;
    ConsensusState _state = ConsensusState::VOID;

private:

    static constexpr uint8_t QUORUM_SIZE = 4;

    void ProcessMessage(const PrepareMessage & message);
    void ProcessMessage(const CommitMessage & message);

    bool ReachedQuorum();

    template<typename MSG>
    bool ProceedWithMessage(const MSG & message, ConsensusState expected_state);

    void AdvanceState(ConsensusState new_state);

    virtual void OnConsensusReached() = 0;

    Log &   _log;
    uint8_t _consensus_count = 0;
};
