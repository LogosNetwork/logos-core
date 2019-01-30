#include <logos/consensus/request/request_handler.hpp>
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

void RequestHandler::OnPostCommit(const RequestBlock & block)
{
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

RequestHandler::PrePrepare & RequestHandler::PrepareNextBatch()
{
    _current_batch = PrePrepare();
    auto & sequence = _requests.get<0>();

    for(auto pos = sequence.begin(); pos != sequence.end(); ++pos)
    {
        // Null state blocks are used as batch delimiters. When
        // one is encountered, remove it from the requests
        // container and close the batch.

        LOG_DEBUG (_log) << "RequestHandler::PrepareNextBatch requests_size="
                         << sequence.size();

        if(pos->account.is_zero() && pos->transactions.size() == 0)
        {
            sequence.erase(pos);
            break;
        }

        if(!_current_batch.AddRequest(*pos))
        {
            LOG_DEBUG (_log) << "RequestHandler::PrepareNextBatch batch full";
            break;
        }
    }

    return _current_batch;
}

auto RequestHandler::GetCurrentBatch() -> PrePrepare &
{
    LOG_DEBUG (_log) << "RequestHandler::GetCurrentBatch - "
                     << "batch_size = "
                     << _current_batch.requests.size();

    return _current_batch;
}

void RequestHandler::InsertFront(const std::list<Send> & requests)
{
    auto & sequenced = _requests.get<0>();

    sequenced.insert(sequenced.begin(), requests.begin(), requests.end());
}

void RequestHandler::Acquire(const PrePrepare & batch)
{
    auto & sequenced = _requests.get<0>();
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < batch.requests.size(); ++pos)
    {
        auto & request = *batch.requests[pos];

        if(hashed.find(request.GetHash()) == hashed.end())
        {
            sequenced.push_back(static_cast<Send&>(request));
        }
    }
}

void RequestHandler::PopFront()
{
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < _current_batch.requests.size(); ++pos)
    {
        hashed.erase(_current_batch.requests[pos]->GetHash());
    }

    _current_batch = PrePrepare();
}

bool RequestHandler::BatchFull()
{
    return _current_batch.requests.size() == CONSENSUS_BATCH_SIZE;
}

bool RequestHandler::Empty()
{
    return _requests.empty();
}

bool RequestHandler::Contains(const BlockHash & hash)
{
    auto & hashed = _requests.get<1>();

    return hashed.find(hash) != hashed.end();
}
