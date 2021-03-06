#include "block_cache.hpp"

namespace logos {

BlockCache::BlockCache(boost::asio::io_service & service, Store &store, std::queue<BlockHash> *unit_test_q)
    : _store(store)
    , _write_q(service, store, this, unit_test_q)
    , _block_container(_write_q)
{
}

BlockCache::add_result BlockCache::AddEpochBlock(EBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:E:" << block->CreateTip().to_string();

    if (!_write_q.VerifyAggSignature(block))
    {
        LOG_ERROR(_log) << "BlockCache::AddEpochBlock: VerifyAggSignature failed";
        return add_result::FAILED;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::AddEpochBlock: BlockExists";
        return add_result::EXISTS;
    }

    if (_block_container.AddEpochBlock(block, false))
    {
        Validate();
    }

    return add_result::OK;
}

BlockCache::add_result BlockCache::AddMicroBlock(MBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:M:" << block->CreateTip().to_string();

    if (!_write_q.VerifyAggSignature(block))
    {
        LOG_ERROR(_log) << "BlockCache::AddMicroBlock: VerifyAggSignature failed";
        return add_result::FAILED;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::AddMicroBlock: BlockExists";
        return add_result::EXISTS;
    }

    if (_block_container.AddMicroBlock(block, false))
    {
        Validate();
    }

    return add_result::OK;
}

BlockCache::add_result BlockCache::AddRequestBlock(RBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:R:" << block->CreateTip().to_string();

    if (!_write_q.VerifyAggSignature(block))
    {
        LOG_ERROR(_log) << "BlockCache::AddRequestBlock: VerifyAggSignature failed";
        return add_result::FAILED;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::AddRequestBlock: BlockExists";
        return add_result::EXISTS;
    }

    if (_block_container.AddRequestBlock(block, false))
    {
        Validate(block->primary_delegate);
    }

    return add_result::OK;
}

void BlockCache::StoreEpochBlock(EBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:E:" << block->CreateTip().to_string();

    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::StoreEpochBlock: BlockExists";
        return;
    }
    if (_block_container.AddEpochBlock(block, true))
    {
        Validate();
    }
}

void BlockCache::StoreMicroBlock(MBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:M:" << block->CreateTip().to_string();

    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::StoreMicroBlock: BlockExists";
        return;
    }
    if (_block_container.AddMicroBlock(block, true))
    {
        Validate();
    }
}

void BlockCache::StoreRequestBlock(RBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:R:" << block->CreateTip().to_string();

    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::StoreRequestBlock: BlockExists";
        return;
    }
    if (_block_container.AddRequestBlock(block, true))
    {
        Validate(block->primary_delegate);
    }
}

void BlockCache::ProcessDependencies(EBPtr block)
{
    if (_block_container.MarkAsValidated(block))
    {
        Validate();
    }
}

void BlockCache::ProcessDependencies(MBPtr block)
{
    if (_block_container.MarkAsValidated(block))
    {
        Validate();
    }
}

void BlockCache::ProcessDependencies(RBPtr block)
{
    if (_block_container.MarkAsValidated(block))
    {
        Validate();
    }
}

bool BlockCache::IsBlockCached(const BlockHash &hash)
{
    LOG_TRACE(_log) << "BlockCache::" << __func__ << ":" << hash.to_string();
    return _block_container.IsBlockCached(hash);
}

bool BlockCache::IsBlockCachedOrQueued(const BlockHash &hash)
{
    LOG_TRACE(_log) << "BlockCache::" << __func__ << ":" << hash.to_string();
    return _block_container.IsBlockCachedOrQueued(hash);
}

void BlockCache::Validate(uint8_t rb_idx)
{
    LOG_TRACE(_log) << "BlockCache::"<<__func__<<"{";
    assert(rb_idx<=NUM_DELEGATES);

    PendingBlockContainer::ChainPtr ptr;
    bool success = false;

    while (_block_container.GetNextBlock(ptr, rb_idx, success))
    {
        if (ptr.rptr)
        {
            RBPtr &block = ptr.rptr->block;
            ValidationStatus &status = ptr.rptr->status;
            LOG_TRACE(_log) << "BlockCache::"<<__func__
                            << (ptr.rptr->direct_write ? ":R:direct writing " : ":R:verifying ")
                            << block->CreateTip().to_string();

            if ((success = ptr.rptr->direct_write || _write_q.VerifyContent(block, &status)))
            {
                _write_q.StoreBlock(block);
                _block_container.BlockDelete(block->Hash());
            }
            else
            {
                //TODO order of validation
                LOG_TRACE(_log) << "BlockCache::Validate RB status: "
                        << ProcessResultToString(status.reason);
                switch (ProcessResultToDependency(status.reason))
                {
                case logos::process_result_dependency::previous_block:
                    _block_container.AddHashDependency(block->previous, ptr);
                    break;
                case logos::process_result_dependency::general_error_code:
                    for (uint32_t i = 0; i < block->requests.size(); ++i)
                    {
                        switch (ProcessResultToDependency(status.requests[i]))
                        {
                        case logos::process_result_dependency::progress:
                            continue;
                        case logos::process_result_dependency::previous_block:
                            _block_container.AddHashDependency(block->requests[i]->previous, ptr);
                            break;
                        case logos::process_result_dependency::sender_account:
                            _block_container.AddHashDependency(block->requests[i]->GetAccount(), ptr);
                            break;
                        default:
                            LOG_FATAL(_log) << "BlockCache::Validate RB status: request i=" << i
                                            << " error_code=" << ProcessResultToString(status.requests[i])
                                            << " block " << block->CreateTip().to_string();
                            //TODO should not be here unless bad delegate set
                            trace_and_halt();
                            break;
                        }
                    }
                    break;
                default:
                    //Since the agg-sigs are already verified,
                    //we expect gap-like reasons.
                    //For any other reason, we log them, and investigate.
                    LOG_FATAL(_log) << "BlockCache::Validate RB status: "
                                    << ProcessResultToString(status.reason)
                                    << " block " << block->CreateTip().to_string();
                    //TODO should not be here unless bad delegate set
                    trace_and_halt();
                    //Throw the block out, otherwise it blocks the rest.
                    //_block_container.BlockDelete(block->Hash());//not enough
                    //success = true;
                    //TODO recall?
                    //TODO detect double spend?
                    break;
                }
            }
        }
        else if (ptr.mptr)
        {
            MBPtr &block = ptr.mptr->block;
            ValidationStatus &status = ptr.mptr->status;
            LOG_TRACE(_log) << "BlockCache::"<<__func__
                            << (ptr.mptr->direct_write ? ":M:direct writing " : ":M:verifying ")
                            << block->CreateTip().to_string();
            if ((success = ptr.mptr->direct_write || _write_q.VerifyContent(block, &status)))
            {
                _write_q.StoreBlock(block);
                _block_container.BlockDelete(block->Hash());
            }
            else
            {
                LOG_TRACE(_log) << "BlockCache::Validate MB status: "
                        << ProcessResultToString(status.reason);
                switch (ProcessResultToDependency(status.reason))
                {
                case logos::process_result_dependency::previous_block:
                    _block_container.AddHashDependency(block->previous, ptr);
                    break;
                case logos::process_result_dependency::general_error_code:
                    for(uint32_t i = 0; i < NUM_DELEGATES; ++i)
                    {
                        switch (ProcessResultToDependency(status.requests[i]))
                        {
                        case logos::process_result_dependency::previous_block:
                            _block_container.AddHashDependency(block->tips[i].digest, ptr);
                            break;
                        default:
                            break;
                        }
                    }
                    break;
                default:
                    LOG_FATAL(_log) << "BlockCache::Validate MB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block->CreateTip().to_string();
                    trace_and_halt();
//                    _block_container.BlockDelete(block->Hash());
//                    success = true;
                    //TODO recall?
                    break;
                }
            }
        }
        else if (ptr.eptr)
        {
            EBPtr &block = ptr.eptr->block;
            ValidationStatus &status = ptr.eptr->status;
            LOG_TRACE(_log) << "BlockCache::"<<__func__
                            << (ptr.eptr->direct_write ? ":E:direct writing " : ":E:verifying ")
                            << block->CreateTip().to_string();
            if ((success = ptr.eptr->direct_write || _write_q.VerifyContent(block, &status)))
            {
                _write_q.StoreBlock(block);
                _block_container.BlockDelete(block->Hash());
                LOG_INFO(_log) << "BlockCache::Validate, store EB, block: "
                        << block->CreateTip().to_string();
            }
            else
            {
                LOG_TRACE(_log) << "BlockCache::Validate EB status: "
                        << ProcessResultToString(status.reason);
                switch (ProcessResultToDependency(status.reason))
                {
                case logos::process_result_dependency::previous_block:
                    _block_container.AddHashDependency(block->previous, ptr);
                    break;
                case logos::process_result_dependency::last_microblock:
                    _block_container.AddHashDependency(block->micro_block_tip.digest, ptr);
                    break;
                default:
                    LOG_FATAL(_log) << "BlockCache::Validate EB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block->CreateTip().to_string();
                    trace_and_halt();
//                    _block_container.BlockDelete(block->Hash());
//                    success = true;
                    //TODO recall?
                    break;
                }
            }
        }
        else
        {
            assert("internal error");
            break;
        }
    }

    LOG_TRACE(_log) << "BlockCache::"<<__func__<<"}";
}

}
