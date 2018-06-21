#include <rai/consensus/primary_delegate.hpp>

constexpr uint8_t PrimaryDelegate::QUORUM_SIZE;

PrimaryDelegate::PrimaryDelegate(Log & log)
    : log_(log)
{}

void PrimaryDelegate::OnConsensusMessage(const PrepareMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE))
    {
        state_ = ConsensusState::POST_PREPARE;
        consensus_count_ = 0;

        PostPrepareMessage response;
        Send(&response, sizeof(response));
    }
}

void PrimaryDelegate::OnConsensusMessage(const CommitMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::POST_PREPARE))
    {
        state_ = ConsensusState::POST_COMMIT;
        consensus_count_ = 0;

        PostCommitMessage response;
        Send(&response, sizeof(response));
    }
}

template<typename Msg>
bool PrimaryDelegate::Validate(const Msg & msg)
{
    return true;
}

bool PrimaryDelegate::ReachedQuorum(uint8_t count)
{
    return count >= QUORUM_SIZE;
}

template<typename MSG>
bool PrimaryDelegate::ProceedWithMessage(const MSG & message, ConsensusState expected_state)
{
    if(state_ != expected_state)
    {
        BOOST_LOG(log_) << "PrimaryDelegate - Error! Received "
                        << MessageToName(message)
                        << " message while in "
                        << StateToString(state_);

        return false;
    }

    if(Validate(message))
    {
        consensus_count_++;
    }
    else
    {
        return false;
    }

    if(ReachedQuorum(consensus_count_))
    {
        return true;
    }

    return false;
}

void PrimaryDelegate::AdvanceState(ConsensusState & state, uint8_t increment)
{
    if(state == ConsensusState::VOID)
    {
        state = ConsensusState::PRE_PREPARE;
    }
    else
    {
        state = static_cast<ConsensusState>(uint8_t(state) + increment);
    }

    consensus_count_ = 0;
}
