#include <logos/consensus/batchblock/request_handler.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>

RequestHandler::RequestHandler()
{
    // After startup consensus is performed
    // with an empty batch block.

    _requests.get<0>().push_back(logos::state_block());
}

void RequestHandler::OnRequest(std::shared_ptr<logos::state_block> block)
{
    LOG_DEBUG(_log) << "RequestHandler - queued request with hash "
                    << block->hash().to_string();
    std::lock_guard<std::mutex> lock(_mutex);
    _requests.get<0>().push_back(*block);
}

void RequestHandler::OnPostCommit(const BatchStateBlock & batch)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < batch.block_count; ++pos)
    {
        auto hash = batch.blocks[pos].hash();

        if(hashed.find(hash) != hashed.end())
        {
            hashed.erase(hash);
        }
    }
}

BatchStateBlock & RequestHandler::GetCurrentBatch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _current_batch;
}

BatchStateBlock & RequestHandler::PrepareNextBatch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _current_batch = BatchStateBlock();

    auto & sequence = _requests.get<0>();
    auto & count = _current_batch.block_count;

    for(auto pos = sequence.begin(); pos != sequence.end(); ++pos)
    {

        // Null state blocks are used as batch delimiters. When
        // one is encountered, remove it from the requests
        // container and close the batch.
        if(pos->hashables.account.is_zero() && pos->hashables.link.is_zero())
        {
            sequence.erase(pos);
            break;
        }

        new(&_current_batch.blocks[count++]) logos::state_block(*pos);

        if(count == CONSENSUS_BATCH_SIZE)
        {
            break;
        }
    }

    return _current_batch;
}

void RequestHandler::InsertFront(const std::list<logos::state_block> & blocks)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequenced = _requests.get<0>();

    sequenced.insert(sequenced.begin(), blocks.begin(), blocks.end());
}

void RequestHandler::Acquire(const BatchStateBlock & batch)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequenced = _requests.get<0>();
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < batch.block_count; ++pos)
    {
        auto & block = batch.blocks[pos];

        if(hashed.find(block.hash()) == hashed.end())
        {
            sequenced.push_back(block);
        }
    }
}

void RequestHandler::PopFront()
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < _current_batch.block_count; ++pos)
    {
        hashed.erase(_current_batch.blocks[pos].hash());
    }

    _current_batch = BatchStateBlock();
}

bool RequestHandler::BatchFull()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _current_batch.block_count == CONSENSUS_BATCH_SIZE;
}

bool RequestHandler::Empty()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _requests.empty();
}

bool RequestHandler::Contains(const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _requests.get<1>();

    return hashed.find(hash) != hashed.end();
}
