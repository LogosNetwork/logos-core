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

    void OnConsensusMessage(const PrepareMessage & message);
    void OnConsensusMessage(const CommitMessage & message);

    template<typename Msg>
    bool Validate(const Msg & msg);

    virtual ~PrimaryDelegate()
    {}

    virtual void Send(void * data, size_t size) = 0;

protected:

    ConsensusState state_ = ConsensusState::VOID;

private:

    static constexpr uint8_t QUORUM_SIZE = 4;

    bool ReachedQuorum(uint8_t count);

    template<typename MSG>
    bool ProceedWithMessage(const MSG & message, ConsensusState expected_state);

    void AdvanceState(ConsensusState & state, uint8_t increment = 2);

    Log &   log_;
    uint8_t consensus_count_ = 0;
};
