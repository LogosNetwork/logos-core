#include "block_write_queue.hpp"
#include "block_cache.hpp"

namespace logos
{

BlockWriteQueue::BlockWriteQueue(Store &store, BlockCache *cache, std::queue<BlockHash> *unit_test_q)
    : _eb_handler(store)
    , _mb_handler(store)
    , _rb_handler(store)
    , _terminate(false)
    , _block_cache(cache)
    , _unit_test_q(unit_test_q)
    , _write_thread(&BlockWriteQueue::WriteThread, this)
{
}

BlockWriteQueue::~BlockWriteQueue()
{
    _terminate = true;
    _write_sem.notify();
    _write_thread.join();
}

bool BlockWriteQueue::VerifyAggSignature(EBPtr block)
{
    if (_unit_test_q) return true;
    return _eb_handler.VerifyAggSignature(*block);
}

bool BlockWriteQueue::VerifyAggSignature(MBPtr block)
{
    if (_unit_test_q) return true;
    return _mb_handler.VerifyAggSignature(*block);
}

bool BlockWriteQueue::VerifyAggSignature(RBPtr block)
{
    if (_unit_test_q) return true;
    return _rb_handler.VerifyAggSignature(*block);
}

bool BlockWriteQueue::VerifyContent(EBPtr block, ValidationStatus *status)
{
    bool res = _eb_handler.VerifyContent(*block, status);
    if (_unit_test_q && !res && status->reason == process_result::not_delegate)
    {
        res = true;
        status->reason = process_result::progress;
    }
    return res;
}

bool BlockWriteQueue::VerifyContent(MBPtr block, ValidationStatus *status)
{
    return _mb_handler.VerifyContent(*block, status);
}

bool BlockWriteQueue::VerifyContent(RBPtr block, ValidationStatus *status)
{
    return _rb_handler.VerifyContent(*block, status);
}

bool BlockWriteQueue::IsBlockCached(const BlockHash &hash)
{
    std::lock_guard<std::mutex> lck (_q_mutex);
    return _q_cache.find(hash) != _q_cache.end();
}

bool BlockWriteQueue::BlockExists(EBPtr block)
{
    return IsBlockCached(block->Hash()) || _eb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(MBPtr block)
{
    return IsBlockCached(block->Hash()) || _mb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(RBPtr block)
{
    return IsBlockCached(block->Hash()) || _rb_handler.BlockExists(*block);
}

void BlockWriteQueue::StoreBlock(BlockPtr ptr)
{
    {
        std::lock_guard<std::mutex> lck (_q_mutex);
        _q.push(ptr);
        _q_cache.insert(ptr.hash);
    }

    _write_sem.notify();
}

void BlockWriteQueue::WriteThread()
{
    BlockPtr ptr;

    for(;;)
    {
        _write_sem.wait();

        if (_terminate)
            break;

        {
            std::lock_guard<std::mutex> lck (_q_mutex);
            if (_q.empty())
                continue;
            ptr = _q.front();
        }

        if (ptr.rptr)
        {
            LOG_TRACE(_log) << "BlockCache:Apply:R: " << ptr.rptr->CreateTip().to_string();
            _rb_handler.ApplyUpdates(*ptr.rptr, ptr.rptr->primary_delegate);
            if (_block_cache)
                _block_cache->ProcessDependencies(ptr.rptr);
            ptr.rptr = nullptr;
        }
        else if (ptr.mptr)
        {
            LOG_TRACE(_log) << "BlockCache:Apply:M: " << ptr.mptr->CreateTip().to_string();
            _mb_handler.ApplyUpdates(*ptr.mptr, ptr.mptr->primary_delegate);
            if (_block_cache)
                _block_cache->ProcessDependencies(ptr.mptr);
            ptr.mptr = nullptr;
        }
        else if (ptr.eptr)
        {
            LOG_TRACE(_log) << "BlockCache:Apply:E: " << ptr.eptr->CreateTip().to_string();
            _eb_handler.ApplyUpdates(*ptr.eptr, ptr.eptr->primary_delegate);
            if (_block_cache)
                _block_cache->ProcessDependencies(ptr.eptr);
            ptr.eptr = nullptr;
        }

        {
            std::lock_guard<std::mutex> lck (_q_mutex);
            _q.pop();
            _q_cache.erase(ptr.hash);
        }

        if (_unit_test_q)
        {
            _unit_test_q->push(ptr.hash);
        }
    }
}

void BlockWriteQueue::StoreBlock(EBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:E:{ " << block->CreateTip().to_string();
    StoreBlock(BlockPtr(block));
    LOG_TRACE(_log) << "BlockCache:Store:E:} " << block->CreateTip().to_string();
}

void BlockWriteQueue::StoreBlock(MBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:M:{ " << block->CreateTip().to_string();
    StoreBlock(BlockPtr(block));
    LOG_TRACE(_log) << "BlockCache:Store:M:} " << block->CreateTip().to_string();
}

void BlockWriteQueue::StoreBlock(RBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:R:{ " << block->CreateTip().to_string();
    StoreBlock(BlockPtr(block));
    LOG_TRACE(_log) << "BlockCache:Store:R:} " << block->CreateTip().to_string();
}

}
