#pragma once

#include <rai/consensus/messages/messages.hpp>

#include <rai/lib/blocks.hpp>

#include <memory>
#include <list>

namespace rai
{
    class alarm;
}

class RequestHandler
{
    using BatchList = std::list<BatchStateBlock>;
    using Handle    = BatchList::iterator;

public:

    void OnRequest(std::shared_ptr<rai::state_block> block);

    bool Empty();
    BatchStateBlock & GetNextBatch();
    void PopFront();
    bool BatchFull();

private:

    void InsertBlock(std::shared_ptr<rai::state_block> block);

    BatchList    _batches;
    Handle       _handle;
    uint8_t      _batch_index  = 0;
};