#pragma once

#include <logos/consensus/messages/messages.hpp>

#include <logos/lib/blocks.hpp>

#include <memory>
#include <list>

class RequestHandler
{
    using BatchList = std::list<BatchStateBlock>;
    using Handle    = BatchList::iterator;

public:

    void OnRequest(std::shared_ptr<logos::state_block> block);

    BatchStateBlock & GetNextBatch();

    void PopFront();
    bool BatchFull();
    bool Empty();

    bool Contains(const logos::block_hash & hash);

private:

    void InsertBlock(std::shared_ptr<logos::state_block> block);

    BatchList _batches;
    Handle    _handle;
    uint64_t  _batch_index = 0;
};
