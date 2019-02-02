/// @file
/// This file contains implementation of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus
#include <logos/consensus/request/request_consensus_manager.hpp>
#include <logos/consensus/request/request_backup_delegate.hpp>
#include <logos/consensus/epoch_manager.hpp>

const boost::posix_time::seconds RequestConsensusManager::ON_CONNECTED_TIMEOUT{10};
RequestHandler RequestConsensusManager::_handler;

RequestConsensusManager::RequestConsensusManager(Service & service,
                                                 Store & store,
                                                 const Config & config,
                                                 MessageValidator & validator,
                                                 EpochEventsNotifier & events_notifier)
    : Manager(service, store, config,
              validator, events_notifier)
    , _init_timer(service)
    , _service(service)
{
    _state = ConsensusState::INITIALIZING;
    _store.batch_tip_get(_delegate_id, _prev_pre_prepare_hash);

    ApprovedBSB block;
    if ( !_prev_pre_prepare_hash.is_zero() && !_store.batch_block_get(_prev_pre_prepare_hash, block))
    {
        _sequence = block.sequence + 1;
    }
}

void
RequestConsensusManager::OnBenchmarkDelegateMessage(
    std::shared_ptr<DelegateMessage> message,
    logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    LOG_DEBUG (_log) << "RequestConsensusManager::OnBenchmarkDelegateMessage() - hash: "
                     << message->GetHash().to_string();

    _using_buffered_blocks = true;
    _buffer.push_back(message);
}

void
RequestConsensusManager::BufferComplete(
  logos::process_return & result)
{
    LOG_DEBUG (_log) << "Buffered " << _buffer.size() << " blocks.";

    result.code = logos::process_result::buffering_done;
    SendBufferedBlocks();
}

std::shared_ptr<MessageParser>
RequestConsensusManager::BindIOChannel(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities & ids)
{
    auto connection = Manager::BindIOChannel(iochannel, ids);

    _connected_vote += _weights[ids.remote].vote_weight;
    _connected_stake += _weights[ids.remote].stake_weight;

    if(ReachedQuorum(_connected_vote,
                     _connected_stake))
    {
        OnDelegatesConnected();
    }

    return connection;
}

void
RequestConsensusManager::SendBufferedBlocks()
{
    logos::process_return unused;

    for(uint64_t i = 0; _buffer.size() && i < CONSENSUS_BATCH_SIZE; ++i)
    {
        OnDelegateMessage(
            static_pointer_cast<DelegateMessage>(
                _buffer.front()
            ),
            unused
        );

        _buffer.pop_front();
    }

    if(!_buffer.size())
    {
        LOG_DEBUG (_log) << "RequestConsensusManager - No more buffered blocks for consensus"
                         << std::endl;
    }
}

bool
RequestConsensusManager::Validate(
  std::shared_ptr<DelegateMessage> message,
  logos::process_return & result)
{
    if(! message->VerifySignature(message->origin))
    {
        LOG_INFO(_log) << "RequestConsensusManager - Validate, bad signature: "
                       << message->signature.to_string()
                       << " account: " << message->origin.to_string();

        result.code = logos::process_result::bad_signature;
        return false;
    }

    auto request = static_pointer_cast<const Request>(message);

    if(!request->Validate(result))
    {
        return false;
    }

    return _persistence_manager.Validate(request, result, false);
}

bool
RequestConsensusManager::ReadyForConsensus()
{
    if(_using_buffered_blocks)
    {
        // TODO: Make sure that RequestHandler::_current_batch has
        //       been prepared before calling _handler.BatchFull.
        //
        return StateReadyForConsensus() && (_handler.BatchFull() ||
                            (_buffer.empty() && !_handler.Empty()));
    }

    return Manager::ReadyForConsensus();
}

void
RequestConsensusManager::QueueMessagePrimary(
    std::shared_ptr<DelegateMessage> message)
{
    _handler.OnRequest(static_pointer_cast<Request>(message));
}

auto
RequestConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    auto & batch = _handler.GetCurrentBatch();

    batch.sequence = _sequence;
    batch.timestamp = GetStamp();
    batch.epoch_number = _events_notifier.GetEpochNumber();

    for(uint64_t i = 0; i < batch.requests.size(); ++i)
    {
        _hashes.insert(batch.requests[i]->GetHash());
    }

    LOG_TRACE (_log) << "RequestConsensusManager::PrePrepareGetNext -"
                     << " batch_size=" << batch.requests.size()
                     << " batch.sequence=" << batch.sequence;

    return batch;
}

auto
RequestConsensusManager::PrePrepareGetCurr() -> PrePrepare &
{
    return _handler.GetCurrentBatch();
}

void
RequestConsensusManager::PrePreparePopFront()
{
    _handler.PopFront();
}

bool
RequestConsensusManager::PrePrepareQueueEmpty()
{
    return _handler.Empty();
}

void
RequestConsensusManager::ApplyUpdates(
  const ApprovedBSB & block,
  uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(block, _delegate_id);
}

uint64_t
RequestConsensusManager::GetStoredCount()
{
    return _handler.GetCurrentBatch().requests.size();
}

void
RequestConsensusManager::InitiateConsensus()
{
    _ne_reject_vote = 0;
    _ne_reject_stake = 0;

    _handler.PrepareNextBatch();
    Manager::InitiateConsensus();
}

void
RequestConsensusManager::OnConsensusReached()
{
    _sequence++;
    Manager::OnConsensusReached();

    LOG_DEBUG (_log) << "RequestConsensusManager::OnConsensusReached _sequence="
            << _sequence;
    if(_using_buffered_blocks)
    {
        SendBufferedBlocks();
    }
}

uint8_t
RequestConsensusManager::DesignatedDelegate(std::shared_ptr<DelegateMessage> message)
{
    // The last five bits of the previous hash
    // (or the account for new accounts) will
    // determine the ID of the designated primary
    // for that account.
    //
    uint8_t indicator = message->previous.is_zero() ?
           message->origin.data()[0] : message->previous.data()[0];

    auto id = uint8_t(indicator & ((1<<DELIGATE_ID_MASK)-1));

    LOG_DEBUG (_log) << "RequestConsensusManager::DesignatedDelegate "
                     << " id=" << (uint)id
                     << " indicator=" << (uint)indicator;

    return id;
}

bool
RequestConsensusManager::PrimaryContains(const BlockHash &hash)
{
    return _handler.Contains(hash);
}

void
RequestConsensusManager::OnPostCommit(const PrePrepare & pre_prepare)
{
    _handler.OnPostCommit(pre_prepare);
    Manager::OnPostCommit(pre_prepare);
}

std::shared_ptr<BackupDelegate<ConsensusType::Request>>
RequestConsensusManager::MakeBackupDelegate(
    std::shared_ptr<IOChannel> iochannel,
    const DelegateIdentities& ids)
{
    return std::make_shared<RequestBackupDelegate>(
            iochannel, *this, *this, _validator,
            ids, _service, _events_notifier, _persistence_manager);
}

void
RequestConsensusManager::AcquirePrePrepare(const PrePrepare & message)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    _handler.Acquire(message);
    OnMessageQueued();
}

void
RequestConsensusManager::OnRejection(
        const Rejection & message)
{
    auto & vote = _weights[_cur_delegate_id].vote_weight;
    auto & stake = _weights[_cur_delegate_id].stake_weight;

    switch(message.reason)
    {
    case RejectionReason::Contains_Invalid_Request:
    {
        auto batch = _handler.GetCurrentBatch();
        auto block_count = batch.requests.size();

        for(uint64_t i = 0; i < block_count; ++i)
        {
            auto & weights = _response_weights[i];

            if(!message.rejection_map[i])
            {
                weights.indirect_vote_support += vote;
                weights.indirect_stake_support += stake;

                weights.supporting_delegates.insert(_cur_delegate_id);

                if(ReachedQuorum(weights.indirect_vote_support + _prepare_vote,
                                 weights.indirect_stake_support + _prepare_stake))
                {
                    _hashes.erase(batch.requests[i]->GetHash());
                }
            }
            else
            {
                weights.reject_vote += vote;
                weights.reject_stake += stake;

                if(Rejected(weights.reject_vote,
                            weights.reject_stake))
                {
                    _hashes.erase(batch.requests[i]->GetHash());
                }
            }
        }

        // All requests have been explicitly
        // rejected or accepted.
        if(_hashes.empty())
        {
            CancelTimer();
            OnPrePrepareRejected();
        }

        break;
    }
    case RejectionReason::New_Epoch:
       _ne_reject_vote += vote;
       _ne_reject_stake += stake;
       break;
    case RejectionReason::Clock_Drift:
    case RejectionReason::Bad_Signature:
    case RejectionReason::Invalid_Previous_Hash:
    case RejectionReason::Wrong_Sequence_Number:
    case RejectionReason::Invalid_Epoch:
    case RejectionReason::Void:
        break;
    }
}

void
RequestConsensusManager::OnStateAdvanced()
{
    _response_weights.fill(Weights());
}

void
RequestConsensusManager::OnPrePrepareRejected()
{
    if (Rejected(_ne_reject_vote, _ne_reject_stake))
    {
        _ne_reject_vote = 0;
        _ne_reject_stake = 0;

        // TODO: Retiring delegate in ForwardOnly state
        //       has to forward to new primary - deferred.
        _events_notifier.OnPrePrepareRejected();

        // forward
        return;
    }

    // Pairs a set of Delegate ID's with indexes,
    // where the indexes represent the requests
    // supported by those delegates.
    using SupportMap = std::pair<std::unordered_set<uint8_t>,
                                 std::unordered_set<uint64_t>>;

    // TODO: Hash the set of delegate ID's to make
    //       lookup constant rather than O(n).
    std::list<SupportMap> subsets;

    auto block_count = _handler.GetCurrentBatch().requests.size();

    // For each request, collect the delegate
    // ID's of those delegates that voted for
    // it.
    for(uint64_t i = 0; i < block_count; ++i)
    {

        // The below condition is true if the set of
        // delegates that approve of the request at
        // index i collectively have enough weight to
        // get this request post-committed.
        if(ReachedQuorum(_prepare_vote + _response_weights[i].indirect_vote_support,
                         _prepare_stake + _response_weights[i].indirect_stake_support))
        {
            // Was any other request approved by
            // exactly the same set of delegates?
            auto entry = std::find_if(
                    subsets.begin(), subsets.end(),
                    [this, i](const SupportMap & map)
                    {
                        return map.first == _response_weights[i].supporting_delegates;
                    });

            // This specific set of supporting delegates
            // doesn't exist yet. Create a new entry.
            if(entry == subsets.end())
            {
                std::unordered_set<uint64_t> indexes;
                indexes.insert(i);

                subsets.push_back(
                        std::make_pair(
                                _response_weights[i].supporting_delegates,
                                indexes));
            }

            // At least one other request was accepted
            // the same set of delegates.
            else
            {
                entry->second.insert(i);
            }
        }
        else
        {
            // Reject the request at index i.
        }
    }

    // Returns true if all elements
    // in set b can be found in set
    // a.
    auto contains = [](const std::unordered_set<uint8_t> & a,
                       const std::unordered_set<uint8_t> & b)
        {
            for(auto e : b)
            {
                if(a.find(e) == a.end())
                {
                    return false;
                }
            }
            return true;
        };

    // Attempt to group requests with overlapping
    // subsets of supporting delegates. This
    // does not find the optimal grouping which
    // would require also considering proper subsets.
    for(auto a = subsets.begin(); a != subsets.end(); ++a)
    {
        auto b = a;
        b++;

        // Compare set A to every set following it
        // in the list.
        for(; b != subsets.end(); ++b)
        {
            auto & a_set = a->first;
            auto & b_set = b->first;

            bool advance = false;

            if(a_set.size() > b_set.size())
            {
                // Does set A contain set B?
                if(contains(a_set, b_set))
                {
                    // Merge sets
                    a->first = b->first;
                    a->second.insert(b->second.begin(),
                                     b->second.end());

                    advance = true;
                }
            }
            else
            {
                // Does set B contain set A?
                if(contains(b_set, a_set))
                {
                    // Merge sets
                    a->second.insert(b->second.begin(),
                                     b->second.end());

                    advance = true;
                }

            }

            // Modifying list while
            // iterating it.
            if(advance)
            {
                auto tmp = b;
                tmp++;
                subsets.erase(b);
                b = tmp;
            }
        }
    }

    std::list<std::shared_ptr<Request>> requests;

    // Create new pre-prepare messages
    // based on the subsets.
    for(auto & subset : subsets)
    {
        auto & batch = _handler.GetCurrentBatch();
        auto & indexes = subset.second;

        uint64_t i = 0;
        auto itr = indexes.begin();

        for(; itr != indexes.end(); ++itr, ++i)
        {
            requests.push_back(batch.requests[*itr]);
        }

        requests.push_back(std::shared_ptr<Request>(new Request()));
    }

    // Pushing a null state_block to the front
    // of the queue will trigger consensus
    // with an empty batch block, which is how
    // we proceed if no requests can be re-proposed.
    if(requests.empty())
    {
        requests.push_back(std::shared_ptr<Request>(new Request()));
    }

    _handler.PopFront();
    _handler.InsertFront(requests);

    _state = ConsensusState::VOID;

    OnMessageQueued();
}

void
RequestConsensusManager::OnDelegatesConnected()
{
    if(_delegates_connected)
    {
        return;
    }

    _delegates_connected = true;

    if (_events_notifier.GetState() == EpochTransitionState::None)
    {
        _init_timer.expires_from_now(ON_CONNECTED_TIMEOUT);
        _init_timer.async_wait([this](const Error &error) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            InitiateConsensus();
        });
    }
    else
    {
        _state = ConsensusState::VOID;
    }
}

void
RequestConsensusManager::OnCurrentEpochSet()
{
    PrimaryDelegate::OnCurrentEpochSet();

    _vote_reject_quorum = _vote_total - _vote_quorum;
    _stake_reject_quorum = _stake_total - _stake_quorum;
}

bool
RequestConsensusManager::Rejected(uint128_t reject_vote, uint128_t reject_stake)
{
    auto op = [](bool & r, uint128_t t, uint128_t q)
              {
                  return r ? t > q
                           : t >= q;
              };

    return op(_vq_rounded, reject_vote, _vote_reject_quorum) &&
           op(_sq_rounded, reject_stake, _stake_reject_quorum);
}
