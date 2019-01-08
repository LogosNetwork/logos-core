#include <logos/consensus/primary_delegate.hpp>
#include <logos/lib/trace.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/asio/error.hpp>
#include <logos/lib/utility.hpp>

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
        OnRejection(message);

        CheckRejection();
    }
}

template<ConsensusType C>
void PrimaryDelegate::ProcessMessage(const PrepareMessage<C> & message)
{
    if(ProceedWithMessage(message, ConsensusState::PRE_PREPARE))
    {
        CycleTimers<C>(true);
        PostPrepareMessage<C> response(_pre_prepare_hash, _post_prepare_sig);
        _post_prepare_hash = response.ComputeHash();
        std::vector<uint8_t> buf;
        response.Serialize(buf);
        Send(buf.data(), buf.size());
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
        PostCommitMessage<C> response(_pre_prepare_hash, _post_commit_sig);

        std::vector<uint8_t> buf;
        response.Serialize(buf);
        Send(buf.data(), buf.size());
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
                    << " timeout expired."
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
    return _validator.Validate(GetHashSigned(message), message.signature, _cur_delegate_id);
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

    auto ceil = [](uint128_t n, uint128_t d, bool & r)
                {
                    uint128_t t = n/d;

                    r = (((n < 0) ^ (d > 0)) &&
                         (n - t*d));

                    t += r;

                    return t;
                };

    _vote_quorum = ceil(2 * _vote_total, 3, _vq_rounded);
    _stake_quorum = ceil(2 * _stake_total, 3, _sq_rounded);
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

void PrimaryDelegate::CancelTimer()
{
    if(!_primary_timer.cancel())
    {
        _timer_cancelled = true;
    }
}

bool PrimaryDelegate::ReachedQuorum(uint128_t vote, uint128_t stake)
{
    auto op = [](bool & r, uint128_t t, uint128_t q)
              {
                  return r ? t >= q
                           : t > q;
              };

    return op(_vq_rounded, vote, _vote_quorum) &&
           op(_sq_rounded, stake, _stake_quorum);
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
    }
    else
    {
        LOG_WARN(_log) << "PrimaryDelegate - Failed to validate signature for "
                       << MessageToName(message)
                       << " while in state: "
                       << StateToString(_state)
                       << std::endl;
        return false;
    }

    if(ReachedQuorum())
    {
        bool good = true;
        if(expected_state == ConsensusState::PRE_PREPARE )
        {
            //need my own sig
            _signatures.push_back({_delegate_id, _pre_prepare_sig});
            _post_prepare_sig.map.reset();
            good = _validator.AggregateSignature(_signatures, _post_prepare_sig);
        }
        else if (expected_state == ConsensusState::POST_PREPARE )
        {
            //need my own sig
            DelegateSig my_commit_sig;
            _validator.Sign(_post_prepare_hash, my_commit_sig);
            _signatures.push_back({_delegate_id, my_commit_sig});
            _post_commit_sig.map.reset();
            good = _validator.AggregateSignature(_signatures, _post_commit_sig);
        }
        else
            good = false;

        if( ! good )
        {
            LOG_FATAL(_log) << "PrimaryDelegate - Failed to after ReachedQuorum() "
                    << " expected_state=" << StateToString(expected_state);
            trace_and_halt();
        }
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
