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
    {
	std::lock_guard<std::mutex> lck (q_mutex);
	if (q_cache.find(block->Hash()) != q_cache.end())
            return true;
    }
    return eb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(MBPtr block)
{
    {
	std::lock_guard<std::mutex> lck (q_mutex);
	if (q_cache.find(block->Hash()) != q_cache.end())
            return true;
    }
    return mb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(RBPtr block)
{
    {
	std::lock_guard<std::mutex> lck (q_mutex);
	if (q_cache.find(block->Hash()) != q_cache.end())
            return true;
    }
    return rb_handler.BlockExists(*block);
}

void BlockWriteQueue::StoreBlock(BlockPtr ptr)
{
    {
	std::lock_guard<std::mutex> lck (q_mutex);
	bool qempty = q.empty();
	q.push(ptr);
	q_cache.insert(ptr.hash);
	if (!qempty) return;
    }
    for (;;)
    {
	if (ptr.rptr)
	{
	    rb_handler.ApplyUpdates(*ptr.rptr, ptr.rptr->primary_delegate);
	}
	else if (ptr.mptr)
	{
	    mb_handler.ApplyUpdates(*ptr.mptr, ptr.mptr->primary_delegate);
	}
	else if (ptr.eptr)
	{
	    eb_handler.ApplyUpdates(*ptr.eptr, ptr.eptr->primary_delegate);
	}
	std::lock_guard<std::mutex> lck (q_mutex);
	q.pop();
	q_cache.erase(ptr.hash);
	if (q.empty())
	    break;
	ptr = q.front();
    }
}

void BlockWriteQueue::StoreBlock(EBPtr block)
{
    StoreBlock(BlockPtr(block));
}

void BlockWriteQueue::StoreBlock(MBPtr block)
{
    StoreBlock(BlockPtr(block));
}

void BlockWriteQueue::StoreBlock(RBPtr block)
{
    StoreBlock(BlockPtr(block));
}

}
