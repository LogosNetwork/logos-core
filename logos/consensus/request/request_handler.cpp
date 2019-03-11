#include <logos/consensus/request/request_handler.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>

RequestHandler::RequestHandler()
{}

void RequestHandler::OnRequest(RequestPtr request)
{
    LOG_DEBUG (_log) << "RequestHandler::OnMessage"
                     << request->ToJson();

    _requests.get<0>().push_back(request);
}

void RequestHandler::OnPostCommit(const RequestBlock & block)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < block.requests.size(); ++pos)
    {
        auto hash = block.requests[pos]->GetHash();

        if(hashed.find(hash) != hashed.end())
        {
            hashed.erase(hash);
        }
    }
}

RequestHandler::PrePrepare & RequestHandler::PrepareNextBatch(
    RequestHandler::Manager & manager,
    bool repropose)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _current_batch = PrePrepare();
    auto & sequence = _requests.get<0>();

    _current_batch.requests.reserve(sequence.size());
    _current_batch.hashes.reserve(sequence.size());
    for(auto pos = sequence.begin(); pos != sequence.end();)
    {
        LOG_DEBUG (_log) << "RequestHandler::PrepareNextBatch requests_size="
                         << sequence.size();

        // 'Null' requests are used as batch delimiters. When
        // one is encountered, remove it from the requests
        // container and close the batch.
        if((*pos)->origin.is_zero() && (*pos)->type == RequestType::Unknown)
        {
            sequence.erase(pos);
            break;
        }

        // Ignore request and erase from primary queue if the request doesn't pass validation
        logos::process_return ignored_result;
        // Don't allow duplicates since we are the primary and should not include old requests
        // unless we are reproposing
        bool allow_duplicates = repropose;
        if(!manager.ValidateAndUpdate(*pos, ignored_result, allow_duplicates))
        {
            pos = sequence.erase(pos);
            continue;
        }

        if(! _current_batch.AddRequest(*pos))
        {
            LOG_DEBUG (_log) << "RequestHandler::PrepareNextBatch batch full";
            break;
        }
        pos++;
    }

    return _current_batch;
}

auto RequestHandler::GetCurrentBatch() -> PrePrepare &
{
    std::lock_guard<std::mutex> lock(_mutex);

    LOG_DEBUG (_log) << "RequestHandler::GetCurrentBatch - "
                     << "batch_size = "
                     << _current_batch.requests.size();

    return _current_batch;
}

void RequestHandler::InsertFront(const std::list<RequestPtr> & requests)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequenced = _requests.get<0>();

    sequenced.insert(sequenced.begin(), requests.begin(), requests.end());
}

void RequestHandler::Acquire(const PrePrepare & batch)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequenced = _requests.get<0>();
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < batch.requests.size(); ++pos)
    {
        auto & request = batch.requests[pos];

        if(hashed.find(request->GetHash()) == hashed.end())
        {
            sequenced.push_back(request);
        }
    }
}

void RequestHandler::PopFront()
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < _current_batch.requests.size(); ++pos)
    {
        hashed.erase(_current_batch.requests[pos]->GetHash());
    }

    _current_batch = PrePrepare();
}

bool RequestHandler::BatchFull()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _current_batch.requests.size() == CONSENSUS_BATCH_SIZE;
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
