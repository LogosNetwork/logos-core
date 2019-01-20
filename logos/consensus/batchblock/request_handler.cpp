#include <logos/consensus/batchblock/request_handler.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>

RequestHandler::RequestHandler()
{
    // After startup consensus is performed
    // with an empty batch block.
    //
    _requests.get<0>().push_back(StateBlock());
}

void RequestHandler::OnRequest(std::shared_ptr<StateBlock> block)
{
    LOG_DEBUG (_log) << "RequestHandler::OnRequest - queued request "
            << block->SerializeJson(false, false);
    std::lock_guard<std::mutex> lock(_mutex);
    _requests.get<0>().push_back(*block);
}

void RequestHandler::OnPostCommit(const BatchStateBlock & batch)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < batch.block_count; ++pos)
    {
        auto hash = batch.blocks[pos].GetHash();

        if(hashed.find(hash) != hashed.end())
        {
            hashed.erase(hash);
        }
    }
}

RequestHandler::BSBPrePrepare & RequestHandler::GetCurrentBatch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    LOG_DEBUG (_log) << "RequestHandler::GetCurrentBatch - "
                     << "batch_size=" << _current_batch.block_count;
    return _current_batch;
}

RequestHandler::BSBPrePrepare & RequestHandler::PrepareNextBatch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _current_batch = BSBPrePrepare();
    auto & sequence = _requests.get<0>();

    for(auto pos = sequence.begin(); pos != sequence.end(); ++pos)
    {
        // Null state blocks are used as batch delimiters. When
        // one is encountered, remove it from the requests
        // container and close the batch.

        LOG_DEBUG (_log) << "RequestHandler::PrepareNextBatch requests_size="
                << sequence.size();

        if(pos->account.is_zero() && pos->GetNumTransactions() == 0)
        {
            sequence.erase(pos);
            break;
        }

        if(! _current_batch.AddStateBlock(*pos))
        {
            LOG_DEBUG (_log) << "RequestHandler::PrepareNextBatch batch full";
            break;
        }
    }

    return _current_batch;
}

void RequestHandler::InsertFront(const std::list<StateBlock> & blocks)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequenced = _requests.get<0>();

    sequenced.insert(sequenced.begin(), blocks.begin(), blocks.end());
}

void RequestHandler::Acquire(const BSBPrePrepare & batch)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequenced = _requests.get<0>();
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < batch.block_count; ++pos)
    {
        auto & block = batch.blocks[pos];

        if(hashed.find(block.GetHash()) == hashed.end())
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
        hashed.erase(_current_batch.blocks[pos].GetHash());
    }

    _current_batch = BSBPrePrepare();
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
