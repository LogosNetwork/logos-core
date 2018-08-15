//===-- logos/consensus/batchblock_consensus_manager.cpp - BatchBlockConsensusManager class implementation -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementation of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus
///
//===----------------------------------------------------------------------===//
#include <logos/consensus/batchblock_consensus_manager.hpp>

void BatchBlockConsensusManager::OnBenchmarkSendRequest(std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>> block, logos::process_return & result)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    BOOST_LOG (_log) << "BatchBlockConsensusManager::OnBenchmarkSendRequest() - hash: " << block->hash().to_string();

    _using_buffered_blocks = true;
    _buffer.push_back(block);
}

void BatchBlockConsensusManager::BufferComplete(logos::process_return & result)
{
    BOOST_LOG(_log) << "Buffered " << _buffer.size() << " blocks.";

    result.code = logos::process_result::buffering_done;
    SendBufferedBlocks();
}

void BatchBlockConsensusManager::SendBufferedBlocks()
{
    logos::process_return unused;

    for(uint64_t i = 0; _buffer.size() && i < CONSENSUS_BATCH_SIZE; ++i)
    {
        OnSendRequest(static_pointer_cast<RequestMessage<ConsensusType::BatchStateBlock>>(_buffer.front()), unused);
        _buffer.pop_front();
    }

    if(!_buffer.size())
    {
        BOOST_LOG (_log) << "BatchBlockConsensusManager - No more buffered blocks for consensus" << std::endl;
    }
}

bool BatchBlockConsensusManager::Validate(std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>> block, logos::process_return & result)
{
	if(logos::validate_message(block->hashables.account, block->hash(), block->signature))
	{
        BOOST_LOG(_log) << "BatchBlockConsensusManager - Validate, bad signature: " << block->signature.to_string()
		                << " account: " << block->hashables.account.to_string();

        result.code = logos::process_result::bad_signature;
        return false;
	}

    return _persistence_manager.Validate(*block, result, _delegate_id);
}

// TBD have to separate specialized part from general consensus processing
bool BatchBlockConsensusManager::ReadyForConsensus_Ext()
{
    if(_using_buffered_blocks)
    {
        return StateReadyForConsensus() && (_handler.BatchFull() ||
                                           (_buffer.empty() && !_handler.Empty()));
    }

    return ReadyForConsensus();
}

void BatchBlockConsensusManager::QueueRequest(std::shared_ptr<RequestMessage<ConsensusType::BatchStateBlock>> request)
{
  _handler.OnRequest(request);
}

PrePrepareMessage<ConsensusType::BatchStateBlock> & BatchBlockConsensusManager::PrePrepare_GetNext()
{
  return reinterpret_cast<PrePrepareMessage<ConsensusType::BatchStateBlock>&>(_handler.GetNextBatch());
}

void BatchBlockConsensusManager::PrePrepare_PopFront()
{
  _handler.PopFront();
}

bool BatchBlockConsensusManager::PrePrepare_QueueEmpty()
{
  return _handler.Empty();
}

bool BatchBlockConsensusManager::PrePrepare_QueueFull()
{
  return _handler.BatchFull();
}

void BatchBlockConsensusManager::ApplyUpdates(const PrePrepareMessage<ConsensusType::BatchStateBlock> & pre_prepare, uint8_t delegate_id)
{
  _persistence_manager.ApplyUpdates(pre_prepare, _delegate_id);
}

uint64_t BatchBlockConsensusManager::OnConsensusReached_StoredCount()
{
  return _handler.GetNextBatch().block_count;
}

bool BatchBlockConsensusManager::OnConsensusReached_Ext()
{
  if(_using_buffered_blocks)
  {
    SendBufferedBlocks();
    return true;
  }

  return false;
}
