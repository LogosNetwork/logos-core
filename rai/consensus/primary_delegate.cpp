#include <rai/consensus/primary_delegate.hpp>

constexpr uint8_t PrimaryDelegate::QUORUM_SIZE;

PrimaryDelegate::PrimaryDelegate(Log & log)
    : _log(log)
{}

void PrimaryDelegate::ProcessMessage(const PrepareMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE))
    {
        AdvanceState();

        PostPrepareMessage response;
        Send(&response, sizeof(response));
    }
}

void PrimaryDelegate::ProcessMessage(const CommitMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::POST_PREPARE))
    {
        AdvanceState();

        PostCommitMessage response;
        Send(&response, sizeof(response));

        OnConsensusReached();
    }
}

template<typename MSG>
bool PrimaryDelegate::Validate(const MSG & msg)
{
    return true;
}

bool PrimaryDelegate::ReachedQuorum()
{
    return _consensus_count >= QUORUM_SIZE;
}

template<typename MSG>
bool PrimaryDelegate::ProceedWithMessage(const MSG & message, ConsensusState expected_state)
{
    if(_state != expected_state)
    {
        BOOST_LOG(_log) << "PrimaryDelegate - Error! Received "
                        << MessageToName(message)
                        << " message while in "
                        << StateToString(_state);

        return false;
    }

    if(Validate(message))
    {
        _consensus_count++;
    }
    else
    {
        return false;
    }

    if(ReachedQuorum())
    {
        return true;
    }

    return false;
}

void PrimaryDelegate::AdvanceState(uint8_t increment)
{
    if(_state == ConsensusState::VOID)
    {
        _state = ConsensusState::PRE_PREPARE;
    }
    else
    {
        _state = static_cast<ConsensusState>(uint8_t(_state) + increment);
    }

    _consensus_count = 0;
}
