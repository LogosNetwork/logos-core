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

    for(uint64_t pos = 0; pos < block.block_count; ++pos)
    {
        auto hash = block.blocks[pos].GetHash();

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

void RequestHandler::InsertFront(const std::list<Send> & requests)
{
    auto & sequenced = _requests.get<0>();

    sequenced.insert(sequenced.begin(), requests.begin(), requests.end());
}

void RequestHandler::Acquire(const PrePrepare & batch)
{
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
    auto & hashed = _requests.get<1>();

    for(uint64_t pos = 0; pos < _current_batch.block_count; ++pos)
    {
        hashed.erase(_current_batch.blocks[pos].GetHash());
    }

    _current_batch = PrePrepare();
}

bool RequestHandler::BatchFull()
{
    return _current_batch.block_count == CONSENSUS_BATCH_SIZE;
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
