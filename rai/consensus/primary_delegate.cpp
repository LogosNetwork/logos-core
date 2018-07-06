#include <rai/consensus/primary_delegate.hpp>

constexpr uint8_t PrimaryDelegate::QUORUM_SIZE;

PrimaryDelegate::PrimaryDelegate(Log & log,
                                 MessageValidator & validator)
    : _log(log)
    , _validator(validator) // NOTE: Don't use _validator in this constructor
                            //       as it's not initialized yet.
{}

void PrimaryDelegate::ProcessMessage(const PrepareMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE))
    {
        AdvanceState(ConsensusState::POST_PREPARE);
        Send<PostPrepareMessage>();
    }
}

void PrimaryDelegate::ProcessMessage(const CommitMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::POST_PREPARE))
    {
        AdvanceState(ConsensusState::POST_COMMIT);
        Send<PostCommitMessage>();
    }
}

template<typename MSG>
bool PrimaryDelegate::Validate(const MSG & message)
{
    return _validator.Validate(message);
}

template<typename MSG>
void PrimaryDelegate::Send()
{
    MSG response;
    _validator.Sign(response);

    Send(&response, sizeof(response));
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

void PrimaryDelegate::AdvanceState(ConsensusState new_state)
{
    _state = new_state;
    _consensus_count = 0;
}
