#include <rai/consensus/request_handler.hpp>

void RequestHandler::OnRequest(std::shared_ptr<rai::state_block> block)
{
    if(_batches.empty())
    {
        _batches.push_back(BatchStateBlock());
        _handle = _batches.begin();
    }

    InsertBlock(block);
}

bool RequestHandler::Empty()
{
    return _batches.empty();
}

void RequestHandler::InsertBlock(std::shared_ptr<rai::state_block> block)
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
