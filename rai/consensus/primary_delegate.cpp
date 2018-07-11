#include <rai/consensus/primary_delegate.hpp>

constexpr uint8_t PrimaryDelegate::QUORUM_SIZE;

PrimaryDelegate::PrimaryDelegate(MessageValidator & validator)
    : _validator(validator) // NOTE: Don't use _validator in this constructor
                            //       as it's not yet initialized.
{}

void PrimaryDelegate::ProcessMessage(const PrepareMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE))
    {
        Send<PostPrepareMessage>();
        AdvanceState(ConsensusState::POST_PREPARE);
    }
}

void PrimaryDelegate::ProcessMessage(const CommitMessage & message)
{
    if(ProceedWithMessage(message, ConsensusState::POST_PREPARE))
    {
        Send<PostCommitMessage>();
        AdvanceState(ConsensusState::POST_COMMIT);
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

    response.hash = _cur_batch_hash;
    _validator.Sign(response, _signatures);

    Send(&response, sizeof(response));
}

void PrimaryDelegate::OnConsensusInitiated(const BlockHash & hash)
{
    _cur_batch_hash = hash;
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
        _signatures.push_back({_cur_delegate_id, message.signature});
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
    _signatures.clear();
}
