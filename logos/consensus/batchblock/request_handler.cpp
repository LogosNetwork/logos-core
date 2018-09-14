#include <logos/consensus/batchblock/request_handler.hpp>

void RequestHandler::OnRequest(std::shared_ptr<logos::state_block> block)
{
    if(_batches.empty())
    {
        _batches.push_back(BatchStateBlock());
        _handle = _batches.begin();
    }

    InsertBlock(block);
}

BatchStateBlock & RequestHandler::GetNextBatch()
{
    return _batches.front();
}

void RequestHandler::PopFront()
{
    if(_batches.size() == 1)
    {
        _batch_index = 0;
    }

    _batches.pop_front();
}

bool RequestHandler::BatchFull()
{
    return _batches.size() && _batches.front().block_count == CONSENSUS_BATCH_SIZE;
}

bool RequestHandler::Empty()
{
    return _batches.empty();
}

void RequestHandler::InsertBlock(std::shared_ptr<logos::state_block> block)
{
    if(_batch_index == CONSENSUS_BATCH_SIZE)
    {
        _batches.push_back(BatchStateBlock());

        _batch_index = 0;
        _handle++;
    }

    _handle->blocks[_batch_index++] = *block;
    _handle->block_count++;
}
