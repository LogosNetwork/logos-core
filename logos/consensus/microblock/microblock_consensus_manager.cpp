#include <logos/consensus/microblock/microblock_consensus_connection.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/lib/trace.hpp>

MicroBlockConsensusManager::MicroBlockConsensusManager(
                               Service & service,
                               Store & store,
                               const Config & config,
                               MessageValidator & validator,
                               ArchiverMicroBlockHandler & handler,
                               EpochEventsNotifier & events_notifier,
                               p2p_interface & p2p)
    : Manager(service, store, config,
	      validator, events_notifier, p2p)
    , _microblock_handler(handler)
    , _enqueued(false)
{
    if (_store.micro_block_tip_get(_prev_hash))
    {
        LOG_FATAL(_log) << "Failed to get microblock's previous hash";
        trace_and_halt();
    }
}

void
MicroBlockConsensusManager::OnBenchmarkSendRequest(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(block);
    LOG_DEBUG (_log) << "MicroBlockConsensusManager::OnBenchmarkSendRequest() - hash: "
                     << block->hash().to_string();
}

bool
MicroBlockConsensusManager::Validate(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    result.code = logos::process_result::progress;

    return true;
}

void
MicroBlockConsensusManager::QueueRequestPrimary(
    std::shared_ptr<Request> request)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(request);
    _enqueued = true;
}

auto
MicroBlockConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    return *_cur_microblock;
}

void
MicroBlockConsensusManager::PrePreparePopFront()
{
    _cur_microblock.reset();
    _enqueued = false;
}

bool
MicroBlockConsensusManager::PrePrepareQueueEmpty()
{
    return !_enqueued;
}

void
MicroBlockConsensusManager::ApplyUpdates(
    const PrePrepare & pre_prepare,
    uint8_t delegate_id)
{
    _persistence_manager.ApplyUpdates(pre_prepare);

    _microblock_handler.OnApplyUpdates(pre_prepare);
}

uint64_t 
MicroBlockConsensusManager::GetStoredCount()
{
    return 1;
}

bool
MicroBlockConsensusManager::PrimaryContains(const logos::block_hash &hash)
{
    return (_cur_microblock && _cur_microblock->hash() == hash);
}

void
MicroBlockConsensusManager::QueueRequestSecondary(std::shared_ptr<Request> request)
{
    _secondary_handler.OnRequest(request,
        boost::posix_time::seconds(_delegate_id * SECONDARY_LIST_TIMEOUT.count()));
}

std::shared_ptr<ConsensusConnection<ConsensusType::MicroBlock>>
MicroBlockConsensusManager::MakeConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        const DelegateIdentities& ids)
{
    return std::make_shared<MicroBlockConsensusConnection>(iochannel, *this, *this,
	    _validator, ids, _microblock_handler, _events_notifier, _persistence_manager, Manager::_consensus_p2p._p2p);
}
