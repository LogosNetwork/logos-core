#include "block_write_queue.hpp"

namespace logos
{

BlockWriteQueue::BlockWriteQueue(Store &store)
    : eb_handler(store)
    , mb_handler(store)
    , rb_handler(store)
{
}

bool BlockWriteQueue::VerifyAggSignature(EBPtr block)
{
    return eb_handler.VerifyAggSignature(*block);
}

bool BlockWriteQueue::VerifyAggSignature(MBPtr block)
{
    return mb_handler.VerifyAggSignature(*block);
}

bool BlockWriteQueue::VerifyAggSignature(RBPtr block)
{
    return rb_handler.VerifyAggSignature(*block);
}

bool BlockWriteQueue::VerifyContent(EBPtr block, ValidationStatus *status)
{
    return eb_handler.VerifyContent(*block, status);
}

bool BlockWriteQueue::VerifyContent(MBPtr block, ValidationStatus *status)
{
    return mb_handler.VerifyContent(*block, status);
}

bool BlockWriteQueue::VerifyContent(RBPtr block, ValidationStatus *status)
{
    return rb_handler.VerifyContent(*block, status);
}

bool BlockWriteQueue::BlockExists(EBPtr block)
{
    return eb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(MBPtr block)
{
    return mb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(RBPtr block)
{
    return rb_handler.BlockExists(*block);
}

void BlockWriteQueue::StoreBlock(EBPtr block)
{
    {
        std::lock_guard<std::mutex> lck (eqmutex);
        bool qempty = ebs.empty();
        ebs.push(block);
        if (!qempty) return;
    }
    for (;;)
    {
        if (!BlockExists(block))
            eb_handler.ApplyUpdates(*block, block->primary_delegate);
        std::lock_guard<std::mutex> lck (eqmutex);
        ebs.pop();
        if (ebs.empty())
            break;
        block = ebs.front();
    }
}

void BlockWriteQueue::StoreBlock(MBPtr block)
{
    {
        std::lock_guard<std::mutex> lck (mqmutex);
        bool qempty = mbs.empty();
        mbs.push(block);
        if (!qempty) return;
    }
    for (;;)
    {
        if (!BlockExists(block))
            mb_handler.ApplyUpdates(*block, block->primary_delegate);
        std::lock_guard<std::mutex> lck (mqmutex);
        mbs.pop();
        if (mbs.empty())
            break;
        block = mbs.front();
    }
}

void BlockWriteQueue::StoreBlock(RBPtr block)
{
    {
        std::lock_guard<std::mutex> lck (rqmutex);
        bool qempty = rbs.empty();
        rbs.push(block);
        if (!qempty) return;
    }
    for (;;)
    {
        if (!BlockExists(block))
            rb_handler.ApplyUpdates(*block, block->primary_delegate);
        std::lock_guard<std::mutex> lck (rqmutex);
        rbs.pop();
        if (rbs.empty())
            break;
        block = rbs.front();
    }
}

}
