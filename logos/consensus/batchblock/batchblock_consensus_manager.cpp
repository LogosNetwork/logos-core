/// @file
/// This file contains implementation of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus
#include <logos/consensus/batchblock/batchblock_consensus_manager.hpp>
#include <logos/consensus/batchblock/bb_consensus_connection.hpp>
#include <logos/consensus/epoch_manager.hpp>

RequestHandler BatchBlockConsensusManager::_handler;

BatchBlockConsensusManager::BatchBlockConsensusManager(
        Service & service,
        Store & store,
        Log & log,
        const Config & config,
        DelegateKeyStore & key_store,
        MessageValidator & validator,
        EpochEventsNotifier & events_notifier)
    : Manager(service, store, log,
              config, key_store, validator, events_notifier)
    , _persistence_manager(store)
    , _service(service)
{
    /* TODO remove once full integration with fallback/rejection is complete
    _state = ConsensusState::INITIALIZING;
     */
}

void
BatchBlockConsensusManager::OnBenchmarkSendRequest(
  std::shared_ptr<Request> block,
  logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    BOOST_LOG (_log) << "BatchBlockConsensusManager::OnBenchmarkSendRequest() - hash: "
                     << block->hash().to_string();

    _using_buffered_blocks = true;
    _buffer.push_back(block);
}

void
BatchBlockConsensusManager::BufferComplete(
  logos::process_return & result)
{
    BOOST_LOG(_log) << "Buffered " << _buffer.size() << " blocks.";

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
                    iochannel, *this, *this, _persistence_manager,
                    _validator, ids, _service, _events_notifier);

    _connections.push_back(connection);

    if(++_channels_bound == QUORUM_SIZE)
    {
        BOOST_LOG(_log) << "CALLING ONDELEGATESCONNECTED()";

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
        BOOST_LOG (_log) << "BatchBlockConsensusManager - No more buffered blocks for consensus"
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
        BOOST_LOG(_log) << "BatchBlockConsensusManager - Validate, bad signature: " 
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
            PrePrepare&>(_handler.GetNextBatch());

    batch.timestamp = GetStamp();
    batch.sequence = _sequence;
    batch.epoch_number = _events_notifier.GetEpochNumber();

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

bool
BatchBlockConsensusManager::PrePrepareQueueFull()
{
    return _handler.BatchFull();
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
    return _handler.GetNextBatch().block_count;
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

std::shared_ptr<ConsensusConnection<ConsensusType::BatchStateBlock>>
BatchBlockConsensusManager::MakeConsensusConnection(
    std::shared_ptr<IOChannel> iochannel,
    const DelegateIdentities& ids)
{
    return std::make_shared<BBConsensusConnection>(
            iochannel, *this, *this, _persistence_manager,
            _validator, ids, _service, _events_notifier);
}

void
BatchBlockConsensusManager::AcquirePrePrepare(const PrePrepare & message)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    _handler.PushBack(message);
}

void
BatchBlockConsensusManager::OnRejection(
        const Rejection & message)
{
    switch(message.reason)
    {
    case RejectionReason::Clock_Drift:
        break;
    case RejectionReason::Contains_Invalid_Request:
    {
        auto block_count = _handler.GetNextBatch().block_count;

        for(uint64_t i = 0; i < block_count; ++i)
        {
            if(!message.rejection_map[i])
            {
                _weights[i].reject_weight++;
                _weights[i].supporting_delegates.insert(_cur_delegate_id);
            }
        }

        break;
    }
    case RejectionReason::Bad_Signature:
        break;
    case RejectionReason::Invalid_Epoch:
        break;
    case RejectionReason::New_Epoch:
        break;
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
    // Pairs a set of Delegate ID's with indexes,
    // where the indexes represent the requests
    // supported by those delegates.
    using SupportMap = std::pair<std::unordered_set<uint8_t>,
                                 std::unordered_set<uint64_t>>;

    // TODO: Hash the set of delegate ID's to make
    //       lookup constant rather than O(n).
    std::list<SupportMap> subsets;

    auto block_count = _handler.GetNextBatch().block_count;

    // For each request, collect the delegate
    // ID's of those delegates that voted for
    // it.
    for(uint64_t i = 0; i < block_count; ++i)
    {
        if(_prepare_weight + _weights[i].reject_weight >= QUORUM_SIZE)
        {
            auto entry = std::find_if(
                    subsets.begin(), subsets.end(),
                    [this, i](const SupportMap & map)
                    {
                        return map.first == _weights[i].supporting_delegates;
                    });

            if(entry == subsets.end())
            {
                std::unordered_set<uint64_t> indexes;
                indexes.insert(i);

                subsets.push_back(
                        std::make_pair(
                                _weights[i].supporting_delegates,
                                indexes));
            }
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

        for(; b != subsets.end(); ++b)
        {
            auto & a_set = a->first;
            auto & b_set = b->first;

            bool advance = false;

            if(a_set.size() > b_set.size())
            {
                if(contains(a_set, b_set))
                {
                    a->first = b->first;
                    a->second.insert(b->second.begin(),
                                     b->second.end());

                    advance = true;
                }
            }
            else
            {
                if(contains(b_set, a_set))
                {
                    a->second.insert(b->second.begin(),
                                     b->second.end());

                    advance = true;
                }

            }

            if(advance)
            {
                auto tmp = b;
                tmp++;
                subsets.erase(b);
                b = tmp;
            }
        }
    }

    std::list<BatchStateBlock> batches;

    // Create new pre-prepare messages
    // based on the subsets.
    for(auto & subset : subsets)
    {
        batches.push_back(BatchStateBlock());
        batches.back().block_count = subset.second.size();

        auto & batch = _handler.GetNextBatch();
        auto & indexes = subset.second;

        uint64_t i = 0;
        auto itr = indexes.begin();

        for(; itr != indexes.end(); ++itr, ++i)
        {
            batches.back().blocks[i] = batch.blocks[*itr];
        }
    }

    _handler.PopFront();
    _handler.InsertFront(batches);

    _state = ConsensusState::PRE_PREPARE;

    OnRequestQueued();
}

void
BatchBlockConsensusManager::OnDelegatesConnected()
{
    // if not in Epoch's transition
    if (_events_notifier.GetState() == EpochTransitionState::None) {
        /* TODO temp to get arround of empty BSB sent on start, while not every delegate is interconnected
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        InitiateConsensus();
        */
    }
}
