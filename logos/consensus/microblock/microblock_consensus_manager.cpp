#include <logos/consensus/microblock/microblock_consensus_connection.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>

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
    queue = 1;
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
    _microblock_handler.CommitToDatabase(pre_prepare);
}

uint64_t 
MicroBlockConsensusManager::GetStoredCount()
{
    return 1;
}
std::shared_ptr<ConsensusConnection<ConsensusType::MicroBlock>>
MicroBlockConsensusManager::MakeConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        PrimaryDelegate* primary,
        MessageValidator& validator,
        const DelegateIdentities& ids)
{
    return std::make_shared<MicroBlockConsensusConnection>(iochannel, primary,
            validator, ids, _microblock_handler);
}
