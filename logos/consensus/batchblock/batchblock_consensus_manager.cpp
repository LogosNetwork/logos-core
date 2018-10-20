/// @file
/// This file contains implementation of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus
#include <logos/consensus/batchblock/batchblock_consensus_manager.hpp>
#include <logos/consensus/batchblock/bb_consensus_connection.hpp>

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
    , _persistence_manager(store, log)
{}

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
                    _validator, ids, _events_notifier);

    _connections.push_back(connection);
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

    return _persistence_manager.Validate(*block, result, _delegate_id);
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
    return reinterpret_cast<PrePrepare&>(
            _handler.GetNextBatch());
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
    logos::uint256_t indicator =  request->hashables.previous.is_zero() ?
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
    return std::make_shared<BBConsensusConnection>(iochannel,
                                                 *this, *this, _persistence_manager,
                                                 _validator, ids, _events_notifier);
}
