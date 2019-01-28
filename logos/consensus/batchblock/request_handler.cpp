#include <logos/common.hpp>
#include <logos/consensus/batchblock/request_handler.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>

RequestHandler::RequestHandler()
{
    // After startup consensus is performed
    // with an empty batch block.
    //

    _requests.get<0>().push_back(Send());
}

void RequestHandler::OnRequest(std::shared_ptr<Send> request)
{
    LOG_DEBUG (_log) << "RequestHandler::OnRequest"
                     << request->ToJson();

    _requests.get<0>().push_back(*request);
}

void RequestHandler::OnPostCommit(const BatchStateBlock & batch)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < batch.block_count; ++pos)
    {
        auto hash = batch.blocks[pos]->GetHash();

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

RequestHandler::BSBPrePrepare & RequestHandler::PrepareNextBatch(
    RequestHandler::Manager & manager)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _current_batch = BSBPrePrepare();
    auto & sequence = _requests.get<0>();

    _current_batch.blocks.reserve(sequence.size());
    _current_batch.hashes.reserve(sequence.size());
    for(auto pos = sequence.begin(); pos != sequence.end();)
    {
        // Null state blocks are used as batch delimiters. When
        // one is encountered, remove it from the requests
        // container and close the batch.

        LOG_DEBUG (_log) << "RequestHandler::PrepareNextBatch requests_size="
                         << sequence.size();

        if(pos->get()->account.is_zero() && pos->get()->GetNumTransactions() == 0)
        {
            sequence.erase(pos);
            break;
        }

        // Ignore request and erase from primary queue if the request doesn't pass validation
        logos::process_return ignored_result;
        // Don't allow duplicates since we are the primary and should not include old requests
        if(!manager.ValidateAndUpdate(static_cast<const Request&>(**pos), ignored_result, false))
        {
            pos = sequence.erase(pos);
            continue;
        }

        if(! _current_batch.AddStateBlock(*pos))
        {
            LOG_DEBUG (_log) << "RequestHandler::PrepareNextBatch batch full";
            break;
        }
        pos++;
    }

    return _current_batch;
}

void RequestHandler::InsertFront(const std::list<Send> & requests)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequenced = _requests.get<0>();

    sequenced.insert(sequenced.begin(), requests.begin(), requests.end());
}

void RequestHandler::Acquire(const BSBPrePrepare & batch)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequenced = _requests.get<0>();
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < batch.block_count; ++pos)
    {
        auto block_ptr = batch.blocks[pos];

        if(hashed.find(block_ptr->GetHash()) == hashed.end())
        {
            sequenced.push_back(block_ptr);
        }
    }
}

void RequestHandler::PopFront()
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < _current_batch.block_count; ++pos)
    {
        hashed.erase(_current_batch.blocks[pos]->GetHash());
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
