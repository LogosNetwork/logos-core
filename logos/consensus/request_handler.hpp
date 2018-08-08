#pragma once

#include <logos/consensus/messages/messages.hpp>

#include <logos/lib/blocks.hpp>

#include <memory>
#include <list>

namespace logos
{
    class alarm;
}

class RequestHandler
{
    using BatchList = std::list<BatchStateBlock>;
    using Handle    = BatchList::iterator;

public:

    void OnRequest(std::shared_ptr<logos::state_block> block);

    bool Empty();
    BatchStateBlock & GetNextBatch();
    void PopFront();
    bool BatchFull();

private:

    void InsertBlock(std::shared_ptr<logos::state_block> block);

    BatchList    _batches;
    Handle       _handle;
    uint64_t     _batch_index  = 0;
};
