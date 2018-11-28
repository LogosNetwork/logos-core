/// @file
/// This file contains implementation of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus
#include <logos/consensus/batchblock/batchblock_consensus_manager.hpp>
#include <logos/consensus/batchblock/bb_consensus_connection.hpp>
#include <logos/consensus/epoch_manager.hpp>

const boost::posix_time::seconds BatchBlockConsensusManager::ON_CONNECTED_TIMEOUT{10};
RequestHandler BatchBlockConsensusManager::_handler;

BatchBlockConsensusManager::BatchBlockConsensusManager(
        Service & service,
        Store & store,
        const Config & config,
        MessageValidator & validator,
	EpochEventsNotifier & events_notifier,
	p2p_interface & p2p)
    : Manager(service, store, config,
	      validator, events_notifier, p2p)
    , _init_timer(service)
    , _service(service)
{
    _state = ConsensusState::INITIALIZING;
    _store.batch_tip_get(_delegate_id, _prev_hash);
}

void
BatchBlockConsensusManager::OnBenchmarkSendRequest(
  std::shared_ptr<Request> block,
  logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    LOG_DEBUG (_log) << "BatchBlockConsensusManager::OnBenchmarkSendRequest() - hash: "
                     << block->hash().to_string();

    _using_buffered_blocks = true;
    _buffer.push_back(block);
}

void
BatchBlockConsensusManager::BufferComplete(
  logos::process_return & result)
{
    LOG_DEBUG (_log) << "Buffered " << _buffer.size() << " blocks.";

    result.code = logos::process_result::buffering_done;
    SendBufferedBlocks();
}

std::shared_ptr<PrequelParser>
BatchBlockConsensusManager::BindIOChannel(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities & ids)
{
    auto connection =
            std::make_shared<BBConsensusConnection>(
                    iochannel, *this, *this, _validator,
		    ids, _service, _events_notifier, _persistence_manager, Manager::_consensus_p2p._p2p);

    _connections.push_back(connection);
	
    if(++_channels_bound == QUORUM_SIZE)
    {
        OnDelegatesConnected();
    }

    return connection;
}

void
BatchBlockConsensusManager::SendBufferedBlocks()
{
    logos::process_return unused;

    for(uint64_t i = 0; _buffer.size() && i < CONSENSUS_BATCH_SIZE; ++i)
    {
        OnSendRequest(
          static_pointer_cast<Request>(
            _buffer.front()), unused);
        _buffer.pop_front();
    }

    if(!_buffer.size())
    {
        LOG_DEBUG (_log) << "BatchBlockConsensusManager - No more buffered blocks for consensus"
                         << std::endl;
    }
}

bool
BatchBlockConsensusManager::Validate(
  std::shared_ptr<Request> block,
  logos::process_return & result)
{
    if(logos::validate_message(block->hashables.account, block->hash(), block->signature))
    {
        LOG_INFO(_log) << "BatchBlockConsensusManager - Validate, bad signature: "
                       << block->signature.to_string()
                       << " account: " << block->hashables.account.to_string();

        result.code = logos::process_result::bad_signature;
        return false;
    }

    return _persistence_manager.Validate(*block, result, false);
}

bool
BatchBlockConsensusManager::ReadyForConsensus()
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
BatchBlockConsensusManager::QueueRequestPrimary(
  std::shared_ptr<Request> request)
{
    _handler.OnRequest(request);
}

auto
BatchBlockConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    auto & batch = reinterpret_cast<
            PrePrepare&>(_handler.GetCurrentBatch());

    batch.sequence = _sequence;
    batch.timestamp = GetStamp();
    batch.epoch_number = _events_notifier.GetEpochNumber();

    for(uint64_t i = 0; i < batch.block_count; ++i)
    {
        _hashes.insert(batch.blocks[i].hash());
    }

    return batch;
}

void
BatchBlockConsensusManager::PrePreparePopFront()
{
    _handler.PopFront();
}

bool
BatchBlockConsensusManager::PrePrepareQueueEmpty()
{
    return _handler.Empty();
}

void
BatchBlockConsensusManager::ApplyUpdates(
  const PrePrepare & pre_prepare,
  uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(pre_prepare, _delegate_id);
}

uint64_t
BatchBlockConsensusManager::GetStoredCount()
{
    return _handler.GetCurrentBatch().block_count;
}

void
BatchBlockConsensusManager::InitiateConsensus()
{
    _new_epoch_rejection_cnt = 0;

    _handler.PrepareNextBatch();

    Manager::InitiateConsensus();
}

void
BatchBlockConsensusManager::OnConsensusReached()
{
    Manager::OnConsensusReached();
    _sequence++;

    if(_using_buffered_blocks)
    {
        SendBufferedBlocks();
    }
}

uint8_t
BatchBlockConsensusManager::DesignatedDelegate(std::shared_ptr<Request> request)
{
    // The last five bits of the previous hash
    // (or the account for new accounts) will
    // determine the ID of the designated primary
    // for that account.
    //
    logos::uint256_t indicator = request->hashables.previous.is_zero() ?
           request->hashables.account.number() :
           request->hashables.previous.number();

    return uint8_t(indicator & ((1<<DELIGATE_ID_MASK)-1));
}

bool
BatchBlockConsensusManager::PrimaryContains(const logos::block_hash &hash)
{
    return _handler.Contains(hash);
}

void
BatchBlockConsensusManager::OnPostCommit(const PrePrepare & block)
{
    _handler.OnPostCommit(block);
    Manager::OnPostCommit(block);
}

std::shared_ptr<ConsensusConnection<ConsensusType::BatchStateBlock>>
BatchBlockConsensusManager::MakeConsensusConnection(
    std::shared_ptr<IOChannel> iochannel,
    const DelegateIdentities& ids)
{
    return std::make_shared<BBConsensusConnection>(
            iochannel, *this, *this, _validator,
	    ids, _service, _events_notifier, _persistence_manager, Manager::_consensus_p2p._p2p);
}

void
BatchBlockConsensusManager::AcquirePrePrepare(const PrePrepare & message)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    _handler.Acquire(message);
    OnRequestQueued();
}

void
BatchBlockConsensusManager::OnRejection(
        const Rejection & message)
{
    switch(message.reason)
    {
    case RejectionReason::Contains_Invalid_Request:
    {
        auto batch = _handler.GetCurrentBatch();
        auto block_count = batch.block_count;

        for(uint64_t i = 0; i < block_count; ++i)
        {
            if(!message.rejection_map[i])
            {
                _weights[i].indirect_support_weight++;
                _weights[i].supporting_delegates.insert(_cur_delegate_id);

                if(_weights[i].indirect_support_weight + _prepare_weight >= QUORUM_SIZE)
                {
                    _hashes.erase(batch.blocks[i].hash());
                }
            }
            else
            {
                // TODO: Replace with total pool of delegate
                //       weights defined by epoch block.
                //
                uint64_t total_weight = 32;

                _weights[i].reject_weight++;

                if(_weights[i].reject_weight > total_weight/3.0)
                {
                    _hashes.erase(batch.blocks[i].hash());
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
       ++_new_epoch_rejection_cnt;
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
BatchBlockConsensusManager::OnStateAdvanced()
{
    _weights.fill(Weights());
}

void
BatchBlockConsensusManager::OnPrePrepareRejected()
{
    if (_new_epoch_rejection_cnt >= QUORUM_SIZE / 3)
    {
        _new_epoch_rejection_cnt = 0;
        // TODO retiring delegate in ForwardOnly state has to forward to new primary - deferred
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

    auto block_count = _handler.GetCurrentBatch().block_count;

    // For each request, collect the delegate
    // ID's of those delegates that voted for
    // it.
    for(uint64_t i = 0; i < block_count; ++i)
    {

        // The below condition is true if the set of
        // delegates that approve of the request at
        // index i collectively have enough weight to
        // get this request post-committed.
        if(_prepare_weight + _weights[i].indirect_support_weight >= QUORUM_SIZE)
        {
            // Was any other request approved by
            // exactly the same set of delegates?
            auto entry = std::find_if(
                    subsets.begin(), subsets.end(),
                    [this, i](const SupportMap & map)
                    {
                        return map.first == _weights[i].supporting_delegates;
                    });

            // This specific set of supporting delegates
            // doesn't exist yet. Create a new entry.
            if(entry == subsets.end())
            {
                std::unordered_set<uint64_t> indexes;
                indexes.insert(i);

                subsets.push_back(
                        std::make_pair(
                                _weights[i].supporting_delegates,
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

    std::list<logos::state_block> requests;

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
            requests.push_back(batch.blocks[*itr]);
        }

        requests.push_back(logos::state_block());
    }

    // Pushing a null state_block to the front
    // of the queue will trigger consensus
    // with an empty batch block, which is how
    // we proceed if no requests can be re-proposed.
    if(requests.empty())
    {
        requests.push_back(logos::state_block());
    }

    _handler.PopFront();
    _handler.InsertFront(requests);

    _state = ConsensusState::VOID;

    OnRequestQueued();
}

void
BatchBlockConsensusManager::OnDelegatesConnected()
{
    LOG_INFO(_log) << "BatchBlockConsensusManager::OnDelegatesConnected";
    if (_events_notifier.GetState() == EpochTransitionState::None)
    {
        LOG_INFO(_log) << "BatchBlockConsensusManager::OnDelegatesConnected 1";
        _init_timer.expires_from_now(ON_CONNECTED_TIMEOUT);
        _init_timer.async_wait([this](const Error &error) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            InitiateConsensus();
        });
    }
    else
    {
        LOG_INFO(_log) << "BatchBlockConsensusManager::OnDelegatesConnected 2";
        _state = ConsensusState::VOID;
    }
}
