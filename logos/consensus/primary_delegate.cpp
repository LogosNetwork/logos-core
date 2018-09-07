#include <logos/consensus/primary_delegate.hpp>

template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::BatchStateBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::BatchStateBlock>&);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::BatchStateBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::Epoch>&);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::Epoch>&);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::Epoch>&);

constexpr uint8_t PrimaryDelegate::QUORUM_SIZE;

PrimaryDelegate::PrimaryDelegate(MessageValidator & validator)
    : _validator(validator) // NOTE: Don't use _validator in this constructor
                            //       as it's not yet initialized.
{}

template<ConsensusType consensus_type>
void PrimaryDelegate::ProcessMessage(const PrepareMessage<consensus_type> & message)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE))
    {
        Send<PostPrepareMessage<consensus_type>>();
        AdvanceState(ConsensusState::POST_PREPARE);
    }
}

template<ConsensusType consensus_type>
void PrimaryDelegate::ProcessMessage(const CommitMessage<consensus_type> & message)
{
    if(ProceedWithMessage(message, ConsensusState::POST_PREPARE))
    {
        Send<PostCommitMessage<consensus_type>>();
        AdvanceState(ConsensusState::POST_COMMIT);

        OnConsensusReached();
    }
}

template<typename MSG>
bool PrimaryDelegate::Validate(const MSG & message)
{
    return _validator.Validate(message, _cur_delegate_id);
}

template<typename MSG>
void PrimaryDelegate::Send()
{
    MSG response(_cur_batch_timestamp);

    response.previous = _cur_batch_hash;
    _validator.Sign(response, _signatures);

    Send(&response, sizeof(response));
}

template<ConsensusType consensus_type>
void PrimaryDelegate::OnConsensusInitiated(const PrePrepareMessage<consensus_type> & block)
{
    BOOST_LOG(_log) << "PrimaryDelegate - Initiating Consensus with PrePrepare hash: " << block.Hash().to_string();

    _cur_batch_hash = block.Hash();
    _cur_batch_timestamp = block.timestamp;
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
        BOOST_LOG(_log) << "PrimaryDelegate - Disregarding message: Received "
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