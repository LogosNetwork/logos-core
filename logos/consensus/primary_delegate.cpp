#include <logos/consensus/primary_delegate.hpp>

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/asio/error.hpp>

template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::BatchStateBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::BatchStateBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::BatchStateBlock>&);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::BatchStateBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::Epoch>&);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::Epoch>&);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::Epoch>&);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::Epoch>&);

const PrimaryDelegate::Seconds PrimaryDelegate::PRIMARY_TIMEOUT{60};
const PrimaryDelegate::Seconds PrimaryDelegate::RECALL_TIMEOUT{300};

PrimaryDelegate::PrimaryDelegate(Service & service,
                                 MessageValidator & validator)
    // NOTE: Don't use _validator in this constructor
    //       as it's not yet initialized.
    //
    : _primary_timer(service)
    , _validator(validator)
    , _recall_timer(service)
{}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const RejectionMessage<C> & message)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE))
    {
        LOG_DEBUG(_log) << "PrimaryDelegate::ProcessMessage - Proceeding to OnRejection";
        OnRejection(message);
    }
}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const PrepareMessage<C> & message)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE))
    {
        CycleTimers<C>(true);

        Send<PostPrepareMessage<C>>();
        AdvanceState(ConsensusState::POST_PREPARE);
    }
    else
    {
        CheckRejection();
    }
}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const CommitMessage<C> & message)
{
    if(ProceedWithMessage(message, ConsensusState::POST_PREPARE))
    {
        CancelTimer();

        Send<PostCommitMessage<C>>();
        AdvanceState(ConsensusState::POST_COMMIT);

        OnConsensusReached();
    }
}

void PrimaryDelegate::OnRejection(const RejectionMessage<ConsensusType::BatchStateBlock> & message)
{}

void PrimaryDelegate::OnRejection(const RejectionMessage<ConsensusType::MicroBlock> & message)
{}

void PrimaryDelegate::OnRejection(const RejectionMessage<ConsensusType::Epoch> & message)
{}

void PrimaryDelegate::CheckRejection()
{
    if(AllDelegatesResponded())
    {
        CancelTimer();
        OnPrePrepareRejected();
    }
}

template<ConsensusType C>
void PrimaryDelegate::OnPrePrepareTimeout(const Error & error)
{
    OnTimeout<C>(error,
                 "PrePrepare",
                 ConsensusState::PRE_PREPARE);
}

template<ConsensusType C>
void PrimaryDelegate::OnPostPrepareTimeout(const Error & error)
{
    OnTimeout<C>(error,
                 "PostPrepare",
                 ConsensusState::POST_PREPARE);
}

template<ConsensusType C>
void PrimaryDelegate::OnTimeout(const Error & error,
        const std::string & timeout,
        ConsensusState expected_state)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto timeout_str = timeout + " (" + ConsensusToName(C) + ")";

    LOG_DEBUG(_log) << timeout_str
                    << " timeout either expired or canceled. "
                    << "Error code: "
                    << error.message();

    if(_timer_cancelled)
    {
        _timer_cancelled = false;
        return;
    }

    if(error)
    {
        if(error == boost::asio::error::operation_aborted)
        {
            return;
        }

        LOG_ERROR(_log) << timeout_str
                        << " timeout - Error: "
                        << error.message();
    }

    if(_state != expected_state)
    {
        LOG_WARN(_log) << timeout_str
                       << " timeout expired during unexpected state."
                       << " state " << StateToString(_state)
                       << " expected state " << StateToString(expected_state)
                       << " Aborting timeout.";
        return;
    }
    LOG_ERROR(_log) << "PrimaryDelegate::Ontimeout<" << ConsensusToName(C) << "> - Delegate going into recall! ";
    _state = ConsensusState::RECALL;
}

template<ConsensusType C>
void PrimaryDelegate::CycleTimers(bool cancel)
{
    if(cancel)
    {
        CancelTimer();
    }

    _primary_timer.expires_from_now(PRIMARY_TIMEOUT);

    if(StateReadyForConsensus())
    {
        _primary_timer.async_wait(
                [this](const Error & error){OnPrePrepareTimeout<C>(error);});
    }
    else
    {
        _primary_timer.async_wait(
                [this](const Error & error){OnPostPrepareTimeout<C>(error);});
    }
}

template<typename M>
bool PrimaryDelegate::Validate(const M & message)
{
    return _validator.Validate(message, _cur_delegate_id);
}

template<typename M>
void PrimaryDelegate::Send()
{
    M response(_cur_batch_timestamp);

    response.previous = _cur_hash;
    _validator.Sign(response, _signatures);

    Send(&response, sizeof(response));
}

void PrimaryDelegate::OnCurrentEpochSet()
{
    for(uint64_t pos = 0; pos < NUM_DELEGATES; ++pos)
    {
        auto & delegate = _current_epoch.delegates[pos];

        _vote_total += delegate.vote;
        _stake_total += delegate.stake;

        _weights[pos] = {delegate.vote, delegate.stake};

        if(pos == _delegate_id)
        {
            _my_vote = delegate.vote;
            _my_stake = delegate.stake;
        }
    }

    SetQuorum(_vote_max_fault, _vote_quorum, _vote_total);
    SetQuorum(_stake_max_fault, _stake_quorum, _stake_total);
}

void PrimaryDelegate::UpdateVotes()
{}

template<ConsensusType C>
void PrimaryDelegate::OnConsensusInitiated(const PrePrepareMessage<C> & block)
{
    LOG_INFO(_log) << "PrimaryDelegate - Initiating Consensus with PrePrepare hash: "
                   << block.Hash().to_string();

    _prepare_vote = _my_vote;
    _prepare_stake = _my_stake;

    _cur_hash = block.Hash();
    _cur_batch_timestamp = block.timestamp;

    CycleTimers<C>();
}

bool PrimaryDelegate::StateReadyForConsensus()
{
    return _state == ConsensusState::VOID || _state == ConsensusState::POST_COMMIT;
}

void PrimaryDelegate::CancelTimer()
{
    if(!_primary_timer.cancel())
    {
        _timer_cancelled = true;
    }
    LOG_DEBUG(_log) << "PrimaryDelegate::CancelTimer - Primary timer canceled. ";
}

void PrimaryDelegate::SetQuorum(uint128_t & max_fault, uint128_t & quorum, const uint128_t & total)
{
#ifdef STRICT_CONSENSUS_THRESHOLD
    quorum = total;
    max_fault = 0;
    LOG_INFO(_log) << "Using strict consensus threshold, total is " << total
                   << " quorum is " << quorum
                   << " max tolerated fault is " << max_fault;
#else
    // SYL integration fix:
    // Per PBFT, we tolerate maximum f = floored((total - 1) / 3) faulty replicas,
    // so quorum size is 2f + 1
    max_fault = static_cast<uint128_t>((total - 1) / 3);
    quorum = max_fault * 2 + 1;
    LOG_INFO(_log) << "Using default consensus threshold, total is " << total
                   << " quorum is " << quorum
                   << " max tolerated fault is " << max_fault;
#endif
}

bool PrimaryDelegate::ReachedQuorum(uint128_t vote, uint128_t stake)
{
    return (vote >= _vote_quorum) && (stake >= _stake_quorum);
}

bool PrimaryDelegate::ReachedQuorum()
{
    return ReachedQuorum(_prepare_vote,
                         _prepare_stake);
}

bool PrimaryDelegate::AllDelegatesResponded()
{
    return _delegates_responded == NUM_DELEGATES - 1;
}

template<typename M>
bool PrimaryDelegate::ProceedWithMessage(const M & message, ConsensusState expected_state)
{
    if(_state != expected_state)
    {
        LOG_INFO(_log) << "PrimaryDelegate - Disregarding message: Received "
                       << MessageToName(message)
                       << " message while in "
                       << StateToString(_state);

        return false;
    }

    // for any message type other than PrePrepare, `previous` field is the current consensus block's hash
    if(_cur_hash != message.previous)
    {
        LOG_INFO(_log) << "PrimaryDelegate - Disregarding message: Received message with hash "
                       << message.previous.to_string()
                       << " while current hash is "
                       << _cur_hash.to_string();

        return false;
    }

    if(Validate(message))
    {
        _delegates_responded++;

        // Rejection message signatures are not
        // aggregated.
        if(message.type != MessageType::Rejection)
        {
            _prepare_vote += _weights[_cur_delegate_id].vote_weight;
            _prepare_stake += _weights[_cur_delegate_id].stake_weight;

            _signatures.push_back({_cur_delegate_id,
                                   message.signature});
        }
        // Immediately proceed to OnRejection
        else
        {
            return true;
        }
    }
    else
    {
        LOG_WARN(_log) << "PrimaryDelegate - Failed to validate signature for "
                       << MessageToName(message)
                       << " while in state: "
                       << StateToString(_state);
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
    _prepare_vote = _my_vote;
    _prepare_stake = _my_stake;
    _delegates_responded = 0;
    _signatures.clear();

    OnStateAdvanced();
}

void PrimaryDelegate::OnStateAdvanced()
{}

void PrimaryDelegate::OnPrePrepareRejected()
{}
