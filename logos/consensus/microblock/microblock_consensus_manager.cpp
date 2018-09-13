#include <logos/consensus/microblock/microblock_consensus_connection.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>

void
MicroBlockConsensusManager::OnBenchmarkSendRequest(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(block);
    BOOST_LOG (_log) << "MicroBlockConsensusManager::OnBenchmarkSendRequest() - hash: " 
                     << block->hash().to_string();
}

bool 
MicroBlockConsensusManager::Validate(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    if (logos::validate_message(block->_delegate, block->hash(), block->_signature))
    {
        BOOST_LOG(_log) << "MicroBlockConsensusManager - Validate, bad signature: "
                        << block->_signature.to_string()
                        << " account: " << block->_delegate.to_string();

        result.code = logos::process_result::bad_signature;
        return false;
    }

    result.code = logos::process_result::progress;

    return true;
}

void 
MicroBlockConsensusManager::QueueRequest(
    std::shared_ptr<Request> request)
{
    _cur_microblock = static_pointer_cast<PrePrepare>(request);
}

auto
MicroBlockConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
    return *_cur_microblock;
}

void 
MicroBlockConsensusManager::PrePreparePopFront()
{
    queue = 0;
}

bool 
MicroBlockConsensusManager::PrePrepareQueueEmpty()
{
    return !queue;
}

bool 
MicroBlockConsensusManager::PrePrepareQueueFull()
{
    return queue;
}

void 
MicroBlockConsensusManager::ApplyUpdates(
    const PrePrepare & pre_prepare,
    uint8_t delegate_id)
{
    _microblock_handler.ApplyUpdates(pre_prepare);
}

uint64_t 
MicroBlockConsensusManager::OnConsensusReachedStoredCount()
{
    return 1;
}

bool 
MicroBlockConsensusManager::OnConsensusReachedExt()
{
    return true;
}

std::shared_ptr<ConsensusConnection<ConsensusType::MicroBlock>>
MicroBlockConsensusManager::MakeConsensusConnection(
        std::shared_ptr<IIOChannel> iochannel,
        PrimaryDelegate* primary,
        DelegateKeyStore& key_store,
        MessageValidator& validator,
        const DelegateIdentities& ids)
{
    return std::make_shared<MicroBlockConsensusConnection>(iochannel, primary,
            key_store, validator, ids, _microblock_handler);
}
