/// @file
/// This file contains specialization of the ConsensusManager class, which
/// handles specifics of Epoch consensus
///
#include <logos/consensus/epoch/epoch_consensus_connection.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>

void 
EpochConsensusManager::OnBenchmarkSendRequest(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    _cur_epoch = static_pointer_cast<PrePrepare>(block);
    BOOST_LOG (_log) << "EpochConsensusManager::OnBenchmarkSendRequest() - hash: " 
                     << block->hash().to_string();
}

bool 
EpochConsensusManager::Validate(
    std::shared_ptr<Request> block,
    logos::process_return & result)
{
    if (logos::validate_message(block->_account, block->hash(), block->_signature))
    {
        BOOST_LOG(_log) << "EpochConsensusManager - Validate, bad signature: "
                        << block->_signature.to_string()
                        << " account: " << block->_account.to_string();

        result.code = logos::process_result::bad_signature;

        return false;
    }

    result.code = logos::process_result::progress;
    return true;
}

void 
EpochConsensusManager::QueueRequest(
    std::shared_ptr<Request> request)
{
    _cur_epoch = static_pointer_cast<PrePrepare>(request);
    queue = 1;
}

auto
EpochConsensusManager::PrePrepareGetNext() -> PrePrepare &
{
  return *_cur_epoch;
}

void 
EpochConsensusManager::PrePreparePopFront()
{
    _cur_epoch.reset();
    queue = 0;
}

bool 
EpochConsensusManager::PrePrepareQueueEmpty()
{
    return !queue;
}

bool 
EpochConsensusManager::PrePrepareQueueFull()
{
    return queue;
}

void 
EpochConsensusManager::ApplyUpdates(
    const PrePrepare & pre_prepare,
    uint8_t delegate_id)
{
    _epoch_handler.CommitToDatabase(pre_prepare);
}

uint64_t 
EpochConsensusManager::GetStoredCount()
{
  return 1;
}

std::shared_ptr<ConsensusConnection<ConsensusType::Epoch>>
EpochConsensusManager::MakeConsensusConnection(
        std::shared_ptr<IOChannel> iochannel,
        PrimaryDelegate* primary,
        MessageValidator& validator,
        const DelegateIdentities& ids)
{
    return std::make_shared<EpochConsensusConnection>(iochannel, primary,
            validator, ids, _epoch_handler);
}
