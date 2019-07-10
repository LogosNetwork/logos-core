#include <logos/consensus/primary_delegate.hpp>
#include <logos/lib/trace.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/asio/error.hpp>
#include <logos/lib/utility.hpp>

// ConsensusType::Request
//
template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::Request>&, uint8_t);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::Request>&, uint8_t);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::Request>&, uint8_t);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::Request>&);
template void PrimaryDelegate::Tally<>(const RejectionMessage<ConsensusType::Request>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::Tally<>(const PrepareMessage<ConsensusType::Request>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::Tally<>(const CommitMessage<ConsensusType::Request>&, uint8_t remote_delegate_id);

// ConsensusType::MicroBlock
//
template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::MicroBlock>&, uint8_t);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::MicroBlock>&, uint8_t);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::MicroBlock>&, uint8_t);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::MicroBlock>&);
template void PrimaryDelegate::Tally<>(const RejectionMessage<ConsensusType::MicroBlock>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::Tally<>(const PrepareMessage<ConsensusType::MicroBlock>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::Tally<>(const CommitMessage<ConsensusType::MicroBlock>&, uint8_t remote_delegate_id);

// ConsensusType::Epoch
//
template void PrimaryDelegate::ProcessMessage<>(const RejectionMessage<ConsensusType::Epoch>&, uint8_t);
template void PrimaryDelegate::ProcessMessage<>(const PrepareMessage<ConsensusType::Epoch>&, uint8_t);
template void PrimaryDelegate::ProcessMessage<>(const CommitMessage<ConsensusType::Epoch>&, uint8_t);
template void PrimaryDelegate::OnConsensusInitiated<>(const PrePrepareMessage<ConsensusType::Epoch>&);
template void PrimaryDelegate::Tally<>(const RejectionMessage<ConsensusType::Epoch>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::Tally<>(const PrepareMessage<ConsensusType::Epoch>&, uint8_t remote_delegate_id);
template void PrimaryDelegate::Tally<>(const CommitMessage<ConsensusType::Epoch>&, uint8_t remote_delegate_id);

const PrimaryDelegate::Seconds PrimaryDelegate::PRIMARY_TIMEOUT{60};
const PrimaryDelegate::Seconds PrimaryDelegate::RECALL_TIMEOUT{300};

PrimaryDelegate::PrimaryDelegate(Service & service,
                                 MessageValidator & validator,
                                 uint32_t epoch_number)
    // NOTE: Don't use _validator in this constructor
    //       as it's not yet initialized.
    //
    : _ongoing(false)
    , _primary_timer(service)
    , _validator(validator)
    , _epoch_number(epoch_number)
    , _recall_timer(service)
{}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const RejectionMessage<C> & message, uint8_t remote_delegate_id)
{
    LOG_DEBUG(_log) << "PrimaryDelegate::ProcessMessage<"
                    << ConsensusToName(C) << ">- Received rejection("
                    << RejectionReasonToName(message.reason)
                    << ")";

    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE, remote_delegate_id) == ProceedAction::REJECTED)
    {
        LOG_DEBUG(_log) << "PrimaryDelegate::ProcessMessage - Proceeding to OnPrePrepareRejected";
        CancelTimer();
        OnPrePrepareRejected();
    }
}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const PrepareMessage<C> & message, uint8_t remote_delegate_id)
{
    switch (ProceedWithMessage(message, ConsensusState::PRE_PREPARE, remote_delegate_id))
    {
        case ProceedAction::APPROVED :
        {
            // We made sure in ProceedWithMessage that only one message can reach here. No need to lock yet
            CycleTimers<C>(true);

            // No need to lock _state_mutex because AdvanceState changes _state then _state_changing, and ProceedWithMessage checks _state_changing first then _state match, so under no circumstance will a wrong message
            PostPrepareMessage<C> response(_pre_prepare_hash, _post_prepare_sig);
            _post_prepare_hash = response.ComputeHash();
            AdvanceState(ConsensusState::POST_PREPARE);
            LOG_INFO(_log) << "PrimaryDelegate::ProcessMessage(Prepare) - APPROVED"
                << "-hash=" << _pre_prepare_hash.to_string();
            // At this point any incoming Prepare messages will still get ignored because of state mismatch
            Send<PostPrepareMessage<C>>(response);

            break;
        }
        case ProceedAction::REJECTED :
        {
            CancelTimer();
            OnPrePrepareRejected();
            break;
        }
        case ProceedAction::DO_NOTHING :
            LOG_INFO(_log) << "PrimaryDelegate::ProcessMessage(Prepare) - DO_NOTHING"
                << "hash=" << _pre_prepare_hash.to_string();
            break;
    }
}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const CommitMessage<C> & message, uint8_t remote_delegate_id)
{
    if(ProceedWithMessage(message, ConsensusState::POST_PREPARE, remote_delegate_id) == ProceedAction::APPROVED)
    {
        CancelTimer();

        PostCommitMessage<C> response(_pre_prepare_hash, _post_commit_sig);
        Send<PostCommitMessage<C>>(response);
        AdvanceState(ConsensusState::POST_COMMIT);
        // At this point, old messages will be ignored for not being in correct state
        OnConsensusReached();
        LOG_INFO(_log) << "PrimaryDelegate::ProcessMessage(Commit) - APPROVED"
            << "-hash=" << _pre_prepare_hash.to_string();
    }
    else
    {
        LOG_INFO(_log) << "PrimaryDelegate::ProcessMessage(Commit) - DO_NOTHING"
            << "hash=" << _pre_prepare_hash.to_string();
    }
}

template<ConsensusType C>
void PrimaryDelegate::Tally(const RejectionMessage<C> & message, uint8_t remote_delegate_id)
{
    LOG_DEBUG(_log) << "PrimaryDelegate::Tally - Tallying for message type " << MessageToName(message);
    OnRejection(message, remote_delegate_id);
}

template<ConsensusType C>
void PrimaryDelegate::Tally(const PrepareMessage<C> & message, uint8_t remote_delegate_id)
{
    TallyStandardPhaseMessage(message, remote_delegate_id);

    // For BatchBlock Prepare messages, we also want to check if each transaction has enough indirect support
    // (in case we encounter any rejection response) to be marked as approved
    TallyPrepareMessage(message, remote_delegate_id);
}

template<ConsensusType C>
void PrimaryDelegate::Tally(const CommitMessage<C> & message, uint8_t remote_delegate_id)
{
    TallyStandardPhaseMessage(message, remote_delegate_id);
}

template<typename M>
void PrimaryDelegate::TallyStandardPhaseMessage(const M & message, uint8_t remote_delegate_id)
{
    LOG_DEBUG(_log) << "PrimaryDelegate::Tally - Tallying for message type " << MessageToName(message);

    //Integration Fix: Need to check that we didn't already tally for this 
    //remote_delegate. Could be a re-broadcast, or could have received message 
    //via direct connection and p2p
    for(size_t i = 0; i < _signatures.size(); ++i)
    {
        if(_signatures[i].delegate_id == remote_delegate_id)
        {
            return;
        }
    } 
    _prepare_vote += _weights[remote_delegate_id].vote_weight;
    _prepare_stake += _weights[remote_delegate_id].stake_weight;

    _signatures.push_back({remote_delegate_id,
                           message.signature});
}

void PrimaryDelegate::TallyPrepareMessage(const PrepareMessage<ConsensusType::Request> & message, uint8_t remote_delegate_id)
{}

void PrimaryDelegate::TallyPrepareMessage(const PrepareMessage<ConsensusType::MicroBlock> & message, uint8_t remote_delegate_id)
{}

void PrimaryDelegate::TallyPrepareMessage(const PrepareMessage<ConsensusType::Epoch> & message, uint8_t remote_delegate_id)
{}

void PrimaryDelegate::OnRejection(const RejectionMessage<ConsensusType::Request> & message, uint8_t remote_delegate_id)
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
                    << "Current pre_prepare_hash: " << _pre_prepare_hash.to_string()
                    << ". Error code: "
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

    OnQuorumFailed();
}

template<ConsensusType C>
void PrimaryDelegate::CycleTimers(bool cancel)
{
    if(cancel)
    {
        CancelTimer();
    }

    _primary_timer.expires_from_now(PRIMARY_TIMEOUT);

    std::weak_ptr<PrimaryDelegate> this_w = shared_from_this();
    if(StateReadyForConsensus())
    {
        _primary_timer.async_wait(
                [this_w](const Error & error){
                    auto this_s = GetSharedPtr(this_w, "PrimaryDelegate<", ConsensusToName(C),
                            ">::CycleTimers, object destroyed");
                    if (!this_s)
                    {
                        return;
                    }
                    this_s->OnPrePrepareTimeout<C>(error);
                });
    }
    else
    {
        _primary_timer.async_wait(
                [this_w](const Error & error){
                    auto this_s = GetSharedPtr(this_w, "PrimaryDelegate<", ConsensusToName(C),
                                               ">::CycleTimers, object destroyed");
                    if (!this_s)
                    {
                        return;
                    }
                    this_s->OnPostPrepareTimeout<C>(error);
                });
    }
}

template<typename M>
bool PrimaryDelegate::Validate(const M & message, uint8_t remote_delegate_id)
{
    return _validator.Validate(GetHashSigned(message), message.signature, remote_delegate_id);
}

void PrimaryDelegate::OnCurrentEpochSet()
{
    for(uint64_t pos = 0; pos < NUM_DELEGATES; ++pos)
    {
        auto & delegate = _current_epoch.delegates[pos];

        _vote_total += delegate.vote.number();
        _stake_total += delegate.stake.number();

        _weights[pos] = {delegate.vote.number(), delegate.stake.number()};

        if(pos == _delegate_id)
        {
            _my_vote = delegate.vote.number();
            _my_stake = delegate.stake.number();
        }
    }

    SetQuorum(_vote_max_fault, _vote_quorum, _vote_total);
    LOG_INFO(_log) << "VOTE:  total is " << _vote_total
                   << " quorum is " << _vote_quorum
                   << " max tolerated fault is " << _vote_max_fault;
    SetQuorum(_stake_max_fault, _stake_quorum, _stake_total);
    LOG_INFO(_log) << "STAKE: total is " << _stake_total
                   << " quorum is " << _stake_quorum
                   << " max tolerated fault is " << _stake_max_fault;
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

    _pre_prepare_hash = block.Hash();
    _validator.Sign(_pre_prepare_hash, _pre_prepare_sig);

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
#else
    // SYL integration fix:
    // Per PBFT, we tolerate maximum f = floored((total - 1) / 3) faulty replicas,
    // so quorum size is 2f + 1
    max_fault = static_cast<uint128_t>((total - 1) / 3);
    quorum = max_fault * 2 + 1;
#endif
}

bool PrimaryDelegate::ReachedQuorum(uint128_t vote, uint128_t stake)
{
    LOG_INFO(_log) << "PrimaryDelegate::ReachedQuorum - _vote_quorum=" << _vote_quorum
        << "-_stake_quorum=" << _stake_quorum << "vote=" << vote << ",stake=" << stake;
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
PrimaryDelegate::ProceedAction PrimaryDelegate::ProceedWithMessage(const M & message, ConsensusState expected_state, uint8_t remote_delegate_id)
{
    // TODO: optimize locking based on read / write; add debugging to Validate
    std::lock_guard<std::mutex> lock(_state_mutex);
    // It's critical to check _state_changing before _state match, since in AdvanceState we change _state before _state_changing
    if (_state_changing)
    {
        // state is already changing, this message came in too late to matter
        LOG_INFO(_log) << "PrimaryDelegate - Disregarding message: Received "
                       << MessageToName(message)
                       << " message while in "
                       << StateToString(_state) << " (state already changing)";

        return ProceedAction::DO_NOTHING;
    }

    LOG_INFO(_log) << "PrimaryDelegate - Received "
        << MessageToName(message)
        << " message while in "
        << StateToString(_state)
        << ", message pre_prepare hash: " << message.preprepare_hash.to_string()
        << ", internal pre_prepare hash: " << _pre_prepare_hash.to_string();

    if(_state != expected_state || message.preprepare_hash != _pre_prepare_hash)
    {
        LOG_INFO(_log) << "PrimaryDelegate - Disregarding message: Received "
                       << MessageToName(message)
                       << " message while in "
                       << StateToString(_state)
                       << ", message pre_prepare hash: " << message.preprepare_hash.to_string()
                       << ", internal pre_prepare hash: " << _pre_prepare_hash.to_string();

        return ProceedAction::DO_NOTHING;
    }

    if(!Validate(message, remote_delegate_id))
    {
        LOG_WARN(_log) << "PrimaryDelegate - Failed to validate signature for "
                       << MessageToName(message) << ", hash " << GetHashSigned(message).to_string()
                       << " while in state: " << StateToString(_state) << ", suspected old consensus round message."
                       << " Received message hash: " << message.preprepare_hash.to_string();
        return ProceedAction::DO_NOTHING;
    }
    _delegates_responded++;

    // SYL Integration fix: use overloaded Tally method to count votes for all standard phase message types
    // (including rejection). For BatchBlocks Prepares, even if the whole doesn't reach quorum we still have to
    // check if the potentially rejected PrePrepare is ready for re-proposal.
    Tally(message, remote_delegate_id);

    // Check if a standard phase message reaches quorum
    // ReachedQuorum returns true iff the whole Block gets enough vote + stake
    if(message.type != MessageType::Rejection && ReachedQuorum())
    {
        LOG_INFO(_log) << "PrimaryDelegate::ProceedAction-Quorum is reached! _pre_prepare_hash=" 
            << _pre_prepare_hash.to_string() << "ConsensusState=" << StateToString(_state);
        bool sig_aggregated = false;
        if(expected_state == ConsensusState::PRE_PREPARE )
        {
            // need my own sig
            _signatures.push_back({_delegate_id, _pre_prepare_sig});
            _post_prepare_sig.map.reset();
            sig_aggregated = _validator.AggregateSignature(_signatures, _post_prepare_sig);
        }
        else if (expected_state == ConsensusState::POST_PREPARE )
        {
            // need my own sig
            DelegateSig my_commit_sig;
            _validator.Sign(_post_prepare_hash, my_commit_sig);
            _signatures.push_back({_delegate_id, my_commit_sig});
            _post_commit_sig.map.reset();
            sig_aggregated = _validator.AggregateSignature(_signatures, _post_commit_sig);
        }

        if( ! sig_aggregated )
        {
            LOG_FATAL(_log) << "PrimaryDelegate - Failed to aggregate signatures"
                            << " expected_state=" << StateToString(expected_state);

            // The BLS key storage or the aggregation code has a fatal error, cannot be
            // used to generate nor verify aggregated signatures. So the local node cannot
            // be a delegate anymore.
            trace_and_halt();
        }
        // set transition flag so only one remote message can trigger state change
        _state_changing = true;
        return ProceedAction::APPROVED;
    }
    // Check if either 1) an incoming rejection can trigger OnPrePrepareRejected, or
    // 2) an incoming pre_prepare for BSB gives indirect support to all transactions inside the batch
    else if (message.type != MessageType::Commit && IsPrePrepareRejected())
    {
        // set transition flag so only one remote message can trigger state change
        _state_changing = true;
        return ProceedAction::REJECTED;
    }


    LOG_INFO(_log) << "PrimaryDelegate::ProceedAction-Quorum not reached! _pre_prepare_hash=" 
        << _pre_prepare_hash.to_string() << "ConsensusState=" << StateToString(_state);
    
    // Then either
    // 1) a standard phase message came in but we haven't reached quorum yet, or
    // 2a) a rejection message came in but hasn't cleared all of the BSB's txns hashes or
    // 2b) we don't have the logic for dealing certain rejection types
    return ProceedAction::DO_NOTHING;
}

void PrimaryDelegate::AdvanceState(ConsensusState new_state)
{
    // TODO: Investigate cache coherency of _state
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

bool PrimaryDelegate::IsPrePrepareRejected()
{
    LOG_WARN(_log) << "PrimaryDelegate::IsPrePrepareRejected - Base method called";
    return false;
}

void PrimaryDelegate::OnPrePrepareRejected()
{}
