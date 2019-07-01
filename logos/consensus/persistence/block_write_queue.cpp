#include "block_write_queue.hpp"

namespace logos
{

BlockWriteQueue::BlockWriteQueue(Store &store, bool unit_test_)
    : eb_handler(store)
    , mb_handler(store)
    , rb_handler(store)
    , unit_test(unit_test_)
{
}

bool BlockWriteQueue::VerifyAggSignature(EBPtr block)
{
    if (unit_test) return true;
    return eb_handler.VerifyAggSignature(*block);
}

bool BlockWriteQueue::VerifyAggSignature(MBPtr block)
{
    if (unit_test) return true;
    return mb_handler.VerifyAggSignature(*block);
}

bool BlockWriteQueue::VerifyAggSignature(RBPtr block)
{
    if (unit_test) return true;
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

bool BlockWriteQueue::IsBlockCached(const BlockHash &hash)
{
    std::lock_guard<std::mutex> lck (q_mutex);
    return q_cache.find(hash) != q_cache.end();
}

bool BlockWriteQueue::BlockExists(EBPtr block)
{
    return IsBlockCached(block->Hash()) || eb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(MBPtr block)
{
    return IsBlockCached(block->Hash()) || mb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(RBPtr block)
{
    return IsBlockCached(block->Hash()) || rb_handler.BlockExists(*block);
}

void BlockWriteQueue::StoreBlock(BlockPtr ptr)
{
    {
        std::lock_guard<std::mutex> lck (q_mutex);
        bool qempty = q.empty();
        q.push(ptr);
        q_cache.insert(ptr.hash);
        if (!qempty)
        {
            return;
        }
    }

    for (;;)
    {
        if (ptr.rptr)
        {
            LOG_TRACE(log) << "BlockCache:Apply:R: " << ptr.rptr->CreateTip().to_string();
            rb_handler.ApplyUpdates(*ptr.rptr, ptr.rptr->primary_delegate);
        }
        else if (ptr.mptr)
        {
            LOG_TRACE(log) << "BlockCache:Apply:M: " << ptr.mptr->CreateTip().to_string();
            mb_handler.ApplyUpdates(*ptr.mptr, ptr.mptr->primary_delegate);
        }
        else if (ptr.eptr)
        {
            LOG_TRACE(log) << "BlockCache:Apply:E: " << ptr.eptr->CreateTip().to_string();
            eb_handler.ApplyUpdates(*ptr.eptr, ptr.eptr->primary_delegate);
        }

        std::lock_guard<std::mutex> lck (q_mutex);
        q.pop();
        q_cache.erase(ptr.hash);
        if (q.empty())
        {
            break;
        }
        ptr = q.front();
    }
}

void BlockWriteQueue::StoreBlock(EBPtr block)
{
    LOG_TRACE(log) << "BlockCache:Store:E:{ " << block->CreateTip().to_string();
    StoreBlock(BlockPtr(block));
    LOG_TRACE(log) << "BlockCache:Store:E:} " << block->CreateTip().to_string();
}

void BlockWriteQueue::StoreBlock(MBPtr block)
{
    LOG_TRACE(log) << "BlockCache:Store:M:{ " << block->CreateTip().to_string();
    StoreBlock(BlockPtr(block));
    LOG_TRACE(log) << "BlockCache:Store:M:} " << block->CreateTip().to_string();
}

void BlockWriteQueue::StoreBlock(RBPtr block)
{
    LOG_TRACE(log) << "BlockCache:Store:R:{ " << block->CreateTip().to_string();
    StoreBlock(BlockPtr(block));
    LOG_TRACE(log) << "BlockCache:Store:R:} " << block->CreateTip().to_string();
}

}
