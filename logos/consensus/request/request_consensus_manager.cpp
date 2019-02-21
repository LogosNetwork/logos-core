/// @file
/// This file contains implementation of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus
#include <logos/consensus/request/request_consensus_manager.hpp>
#include <logos/consensus/request/request_backup_delegate.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/node/delegate_identity_manager.hpp>

const boost::posix_time::seconds RequestConsensusManager::ON_CONNECTED_TIMEOUT{10};
RequestHandler RequestConsensusManager::_handler;

RequestConsensusManager::RequestConsensusManager(Service & service,
                                                 Store & store,
                                                 const Config & config,
                                                 MessageValidator & validator,
                                                 EpochEventsNotifier & events_notifier,
                                                 p2p_interface & p2p)
    : Manager(service, store, config,
	      validator, events_notifier, p2p)
    , _init_timer(service)
{
    _state = ConsensusState::INITIALIZING;
    _store.request_tip_get(_delegate_id, _prev_pre_prepare_hash);

    ApprovedRB block;
    if ( !_prev_pre_prepare_hash.is_zero() && !_store.request_block_get(_prev_pre_prepare_hash, block))
    {
        _sequence = block.sequence + 1;
    }
}

void
RequestConsensusManager::OnBenchmarkDelegateMessage(
    std::shared_ptr<DelegateMessage> message,
    logos::process_return & result)
{
    std::lock_guard<std::mutex> lock(_buffer_mutex);

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

std::shared_ptr<ConsensusMsgSink>
RequestConsensusManager::BindIOChannel(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities & ids)
{
    auto connection = Manager::BindIOChannel(iochannel, ids);

    _connected_vote += _weights[ids.remote].vote_weight;
    _connected_stake += _weights[ids.remote].stake_weight;

    // SYL Integration fix: need to add in our own vote and stake as well
    if(ReachedQuorum(_connected_vote + _my_vote,
                     _connected_stake + _my_stake))
    {
        OnDelegatesConnected();
    }

    return connection;
}

void
RequestConsensusManager::SendBufferedBlocks()
{
    logos::process_return unused;
    std::lock_guard<std::mutex> lock(_buffer_mutex);

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


    return _persistence_manager.ValidateSingleRequest(message, _events_notifier.GetEpochNumber(), result, false);
}

bool
RequestConsensusManager::ReadyForConsensus()
{
    if(_using_buffered_blocks)
    {

        std::lock_guard<std::mutex> lock(_buffer_mutex);
        // TODO: Make sure that RequestHandler::_current_batch has
        //       been prepared before calling _handler.BatchFull.
        //
        return !_ongoing && (_handler.BatchFull() ||
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

// This should only be called once per consensus round
auto
RequestConsensusManager::PrePrepareGetNext() -> PrePrepare &
{


    auto & batch = _handler.GetCurrentBatch();


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
  const ApprovedRB & block,
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
RequestConsensusManager::InitiateConsensus(bool reproposing)
{
    _ne_reject_vote = 0;
    _ne_reject_stake = 0;
    // make sure we start with a fresh set of hashes so as to not interfere with rejection logic
    _hashes.clear();
    _should_repropose = false;

    auto & batch = _handler.GetCurrentBatch();
    batch = PrePrepare();

    batch.sequence = _sequence;
    batch.timestamp = GetStamp();
    batch.epoch_number = _events_notifier.GetEpochNumber();
    batch.primary_delegate = _delegate_id;
    // SYL Integration fix: move previous assignment here to avoid overriding in archive blocks
    batch.previous = _prev_pre_prepare_hash;
    // SYL Integration: perform validation against account_db here instead of at request receive time
    {
        std::lock_guard<std::mutex> lock(_persistence_manager._write_mutex);
        _handler.PrepareNextBatch(_persistence_manager, reproposing);
    }
    Manager::InitiateConsensus(reproposing);
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
                        message->origin.bytes.back() : message->previous.bytes.back();

    auto did = uint8_t(indicator & ((1<<DELIGATE_ID_MASK)-1));

    LOG_DEBUG (_log) << "RequestConsensusManager::DesignatedDelegate "
                     << " id=" << (uint)did
                     << " indicator=" << (uint)indicator;
    return did;
}

bool
RequestConsensusManager::PrimaryContains(const BlockHash &hash)
{
    return _handler.Contains(hash);
}

void
RequestConsensusManager::OnPostCommit(const PrePrepare & pre_prepare)
{
    // SYL integration: don't need locking here because we can safely append to Primary queue,
    // and OnRequestQueued has detection for ongoing consensus round
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
	    ids, _service, _events_notifier, _persistence_manager, GetP2p());
}

void
RequestConsensusManager::AcquirePrePrepare(const PrePrepare & message)
{
    // SYL integration: don't need locking here because we can safely append to Primary queue,
    // and OnRequestQueued has detection for ongoing consensus round
    _handler.Acquire(message);
    OnMessageQueued();
}

void RequestConsensusManager::TallyPrepareMessage(const Prepare & message, uint8_t remote_delegate_id)
{
    if(!_should_repropose)  // We only check individual transactions if we have already seen Rejection messages
    {
        return;
    }

    auto & vote = _weights[remote_delegate_id].vote_weight;
    auto & stake = _weights[remote_delegate_id].stake_weight;

    auto batch = _handler.GetCurrentBatch();
    auto block_count = batch.requests.size();

    for(uint64_t i = 0; i < block_count; ++i)
    {
        auto & weights = _response_weights[i];

        if(ReachedQuorum(weights.indirect_vote_support + _prepare_vote,
                         weights.indirect_stake_support + _prepare_stake))
        {
            _hashes.erase(batch.requests[i]->GetHash());
        }
    }
}

void
RequestConsensusManager::OnRejection(
        const Rejection & message, uint8_t remote_delegate_id)
{
    auto & vote = _weights[remote_delegate_id].vote_weight;
    auto & stake = _weights[remote_delegate_id].stake_weight;

    switch(message.reason)
    {
    case RejectionReason::Contains_Invalid_Request:
    {
        _should_repropose = true;
        auto batch = _handler.GetCurrentBatch();
        auto block_count = batch.requests.size();

        for(uint64_t i = 0; i < block_count; ++i)
        {
            auto & weights = _response_weights[i];

            if(!message.rejection_map[i])
            {
                weights.indirect_vote_support += vote;
                weights.indirect_stake_support += stake;

                weights.supporting_delegates.insert(remote_delegate_id);

                if(ReachedQuorum(weights.indirect_vote_support + _prepare_vote,
                                 weights.indirect_stake_support + _prepare_stake))
                {
                    _hashes.erase(batch.requests[i]->GetHash());
                }
            }
            else
            {
                LOG_WARN(_log) << "BatchBlockConsensusManager::OnRejection - Received rejection for " << batch.requests[i]->GetHash().to_string();
                weights.reject_vote += vote;
                weights.reject_stake += stake;

                if(Rejected(weights.reject_vote,
                            weights.reject_stake))
                {
                    _hashes.erase(batch.requests[i]->GetHash());
                }
            }
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
    case RejectionReason::Invalid_Primary_Index:
    case RejectionReason::Void:
        break;
    }
}

void
RequestConsensusManager::OnStateAdvanced()
{
    _response_weights.fill(Weights());
}

// All requests have been explicitly rejected or accepted.
// Needs _state_mutex locked
bool
RequestConsensusManager::IsPrePrepareRejected()
{
    if(_hashes.empty() && _should_repropose)  // SYL Integration: extra flag prevents mistakenly rejecting an empty BSB
    {
        LOG_DEBUG(_log) << "BatchBlockConsensusManager::OnRejection - all requests in current batch "
                        << "have been explicitly rejected or accepted";
        return true;
    }
    else if (Rejected(_ne_reject_vote, _ne_reject_stake))
    {
        LOG_DEBUG(_log) << "BatchBlockConsensusManager::OnRejection - Rejected because of new epoch";
        return true;
    }
    return false;
}

// This should be called while _state_mutex is still locked.
void
RequestConsensusManager::OnPrePrepareRejected()
{
    if (_state != ConsensusState::PRE_PREPARE)
    {
        LOG_FATAL(_log) << "BatchBlockConsensusManager::OnPrePrepareRejected - unexpected state " << StateToString(_state);
        trace_and_halt();
    }
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
            // by the same set of delegates.
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

    AdvanceState(ConsensusState::VOID);

    // SYL integration fix: this is the only place other than
    // OnConsensusReached where we reset the ongoing status

    // Don't have to change _ongoing because we have to immediately repropose
    InitiateConsensus();
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
            _ongoing = true;
            InitiateConsensus();
        });
    }
    else
    {
        _state = ConsensusState::VOID;
    }
}

bool
RequestConsensusManager::Rejected(uint128_t reject_vote, uint128_t reject_stake)
{
    return (reject_vote > _vote_max_fault) || (reject_stake > _stake_max_fault);
}
