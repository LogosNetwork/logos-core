#include <logos/consensus/request/request_internal_queue.hpp>

bool
RequestInternalQueue::Contains(const BlockHash & hash)
{
    auto & hashed = _requests.get<1>();
    return hashed.find(hash) != hashed.end();
}

bool
RequestInternalQueue::Empty()
{
    return _requests.empty();
}

void
RequestInternalQueue::PushBack(const RequestPtr request)
{
    LOG_DEBUG(_log) << "RequestInternalQueue::PushBack " << request->ToJson();
    _requests.push_back(request);
}

void
RequestInternalQueue::InsertFront(const std::list<RequestPtr> & requests)
{
    auto & sequenced = _requests.get<0>();

    sequenced.insert(sequenced.begin(), requests.begin(), requests.end());
}

void
RequestInternalQueue::PopFront(const PrePrepare & current_batch)
{
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < current_batch.requests.size(); ++pos)
    {
        hashed.erase(current_batch.requests[pos]->GetHash());
    }

    // Need to remove the empty delimiter as well
    auto & sequence = _requests.get<0>();
    auto pos = sequence.begin();
    if((*pos)->origin.is_zero() && (*pos)->type == RequestType::Unknown)
    {
        sequence.erase(pos);
    }
    else {
        LOG_FATAL(_log) << "RequestInternalQueue::PopFront - container data corruption detected, pos data: " << (*pos)->ToJson();
        trace_and_halt();
    }
}