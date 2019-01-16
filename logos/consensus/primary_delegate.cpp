#include <logos/consensus/primary_delegate.hpp>

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/asio/error.hpp>

template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::BatchStateBlock>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::BatchStateBlock>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::BatchStateBlock>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::BatchStateBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::MicroBlock>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::MicroBlock>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::MicroBlock>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::Epoch>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::Epoch>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::Epoch>&, uint8_t remote_delegate_id);
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
void PrimaryDelegate::ProcessMessage(const RejectionMessage<C> & message, uint8_t remote_delegate_id)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE, remote_delegate_id))
    {
        LOG_DEBUG(_log) << "PrimaryDelegate::ProcessMessage - Proceeding to OnRejection";
        OnRejection(message, remote_delegate_id);
    }
}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const PrepareMessage<C> & message, uint8_t remote_delegate_id)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE, remote_delegate_id))
    {
        // We make sure in ProceedWithMessage that only one message can reach here. No need to lock yet
        CycleTimers<C>(true);

        // lock here because if new commit messages come in we don't want to discard them
        // Or maybe use a different lock for this specific purpose?
        std::lock_guard<std::mutex> lock(_state_mutex);
        Send<PostPrepareMessage<C>>();
        AdvanceState(ConsensusState::POST_PREPARE);
    }
    // SYL Integration fix: we won't have to check rejection since if all
    // backup delegates responded and we have not reached quorum, some
    // previous rejection message must have already triggered PrePrepare rejection
}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const CommitMessage<C> & message, uint8_t remote_delegate_id)
{
    if(ProceedWithMessage(message, ConsensusState::POST_PREPARE, remote_delegate_id))
    {
        CancelTimer();

        Send<PostCommitMessage<C>>();
        // Can lock after Send because we won't get response back
        std::lock_guard<std::mutex> lock(_state_mutex);
        AdvanceState(ConsensusState::POST_COMMIT);
        // At this point, new messages will be ignored for not being in correct state
        OnConsensusReached();
    }
}

void PrimaryDelegate::OnRejection(const RejectionMessage<ConsensusType::BatchStateBlock> & message, uint8_t remote_delegate_id)
{}

void PrimaryDelegate::OnRejection(const RejectionMessage<ConsensusType::MicroBlock> & message, uint8_t remote_delegate_id)
{}

void PrimaryDelegate::OnRejection(const RejectionMessage<ConsensusType::Epoch> & message, uint8_t remote_delegate_id)
{}

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
    AdvanceState(ConsensusState::RECALL);
}

template<ConsensusType C>
void PrimaryDelegate::CycleTimers(bool cancel) // This should be called while _state_mutex is still locked.
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
bool PrimaryDelegate::Validate(const M & message, uint8_t remote_delegate_id)
{
    return _validator.Validate(message, remote_delegate_id);
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

void PrimaryDelegate::CancelTimer() // This should be called while _state_mutex is still locked.
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
bool PrimaryDelegate::ProceedWithMessage(const M & message, ConsensusState expected_state, uint8_t remote_delegate_id)
{
    if(Validate(message, remote_delegate_id)) // SYL Integration fix: do the validation first which requires no locking
    {
        // SYL Integration fix: place lock on shared resource,
        std::lock_guard<std::mutex> lock(_state_mutex);

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

        if (!_state_changing)
        {
            _delegates_responded++;

            // Rejection message signatures are not
            // aggregated.
            if(message.type != MessageType::Rejection)
            {
                _prepare_vote += _weights[remote_delegate_id].vote_weight;
                _prepare_stake += _weights[remote_delegate_id].stake_weight;

                _signatures.push_back({remote_delegate_id,
                                       message.signature});
            }
            // Immediately proceed to OnRejection
            else
            {
                return true;
            }

            // set transition flag so only one remote message can trigger state change
            if(ReachedQuorum())
            {
                _state_changing = true;
                return true;
            }

            return false;
        }
        // state is already changing, this message came in too late to matter
        return false;
    }
    else
    {
        LOG_WARN(_log) << "PrimaryDelegate - Failed to validate signature for "
                       << MessageToName(message)
                       << " while in state: "
                       << StateToString(_state);
        return false;
    }
}

// SYL integration: If it is called for transitioning into states *after* Consensus has been started,
// i.e. advancing into states other than PRE_PREPARE, then _state_mutex needs to be locked
void PrimaryDelegate::AdvanceState(ConsensusState new_state)
{
    _state = new_state;
    _prepare_vote = _my_vote;
    _prepare_stake = _my_stake;
    _delegates_responded = 0;
    _signatures.clear();

    OnStateAdvanced();
    _state_changing = false;
}

void PrimaryDelegate::OnStateAdvanced()
{}

void PrimaryDelegate::OnPrePrepareRejected()
{}
