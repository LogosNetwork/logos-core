#include "block_write_queue.hpp"
#include "block_cache.hpp"

namespace logos
{

BlockWriteQueue::BlockWriteQueue(boost::asio::io_service & service, Store &store, BlockCache *cache, std::queue<BlockHash> *unit_test_q)
    : _service(service)
    , _eb_handler(store)
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
    if (_unit_test_q && block->requests.size())
    {
        status->reason = logos::process_result::progress;
        for (int i = 0; i < block->requests.size(); ++i)
        {
            if (block->requests[i]->previous != BlockHash()
                    && _unit_test_requests.find(block->requests[i]->previous) == _unit_test_requests.end())
            {
                status->requests[i] = logos::process_result::gap_previous;
                status->reason = logos::process_result::invalid_request;
            }
            else if (block->requests[i]->origin != AccountAddress()
                    && _unit_test_accounts.find(block->requests[i]->origin) == _unit_test_requests.end()
                    && block->requests[i]->fee == Amount(0))
            {
                status->requests[i] = logos::process_result::insufficient_balance;
                status->reason = logos::process_result::invalid_request;
            }
        }
        return status->reason == logos::process_result::progress;
    }
    else
    {
        return _rb_handler.VerifyContent(*block, status);
    }
}

bool BlockWriteQueue::IsBlockQueued(const BlockHash &hash)
{
    std::lock_guard<std::mutex> lck (_q_mutex);
    return _q_cache.find(hash) != _q_cache.end();
}

bool BlockWriteQueue::BlockExists(EBPtr block)
{
    return IsBlockQueued(block->Hash()) || _eb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(MBPtr block)
{
    return IsBlockQueued(block->Hash()) || _mb_handler.BlockExists(*block);
}

bool BlockWriteQueue::BlockExists(RBPtr block)
{
    return IsBlockQueued(block->Hash()) || _rb_handler.BlockExists(*block);
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
            if (_unit_test_q && ptr.rptr->requests.size())
            {
                for (int i = 0; i < ptr.rptr->requests.size(); ++i)
                {
                    _unit_test_requests.insert(ptr.rptr->requests[i]->Hash());
                    if (ptr.rptr->requests[i]->fee > Amount(0))
                        _unit_test_accounts.insert(ptr.rptr->requests[i]->origin);
                }
            }
            else
            {
                _rb_handler.ApplyUpdates(*ptr.rptr, ptr.rptr->primary_delegate);
            }

            if (_block_cache) {
                if (_unit_test_q) {
                    _block_cache->ProcessDependencies(ptr.rptr);
                } else {
                     _service.post([this, ptr]() {
                        LOG_TRACE(_log) << "-> BlockCache:ProcessDependencies:R: " << ptr.rptr->CreateTip().to_string();
                        this->_block_cache->ProcessDependencies(ptr.rptr);
                    });
                }
            }
            ptr.rptr = nullptr;
        }
        else if (ptr.mptr)
        {
            LOG_TRACE(_log) << "BlockCache:Apply:M: " << ptr.mptr->CreateTip().to_string();
            _mb_handler.ApplyUpdates(*ptr.mptr, ptr.mptr->primary_delegate);
            if (_block_cache)
            {
                if (_unit_test_q)
                {
                    _block_cache->ProcessDependencies(ptr.mptr);
                }
                else
                {
                    _service.post([this, ptr]() {
                        this->_block_cache->ProcessDependencies(ptr.mptr);
                    });
                }
            }
            ptr.mptr = nullptr;
        }
        else if (ptr.eptr)
        {
            LOG_TRACE(_log) << "BlockCache:Apply:E: " << ptr.eptr->CreateTip().to_string();
            _eb_handler.ApplyUpdates(*ptr.eptr, ptr.eptr->primary_delegate);
            if (_block_cache) {
                if (_unit_test_q) {
                    _block_cache->ProcessDependencies(ptr.eptr);
                } else {
                    _service.post([this, ptr]() {
                        this->_block_cache->ProcessDependencies(ptr.eptr);
                    });
                }
            }
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
