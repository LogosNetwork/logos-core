#include "block_cache.hpp"

namespace logos {

BlockCache::BlockCache(Store &store, std::queue<BlockHash> *unit_test_q)
    : _store(store)
    , _write_q(store, unit_test_q)
    , _block_container(_write_q)
{
}

bool BlockCache::AddEpochBlock(EBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:E:" << block->CreateTip().to_string();

    if (!_write_q.VerifyAggSignature(block))
    {
        LOG_ERROR(_log) << "BlockCache::AddEpochBlock: VerifyAggSignature failed";
        return false;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::AAddEpochBlock: BlockExists";
        return true;
    }

    if (_block_container.AddEpochBlock(block))
    {
        Validate();//TODO optimize: Validate eb first
    }

    //TODO remove after integration tests
    _block_container.DumpCachedBlocks();

    return true;
}

bool BlockCache::AddMicroBlock(MBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:M:" << block->CreateTip().to_string();

    if (!_write_q.VerifyAggSignature(block))
    {
        LOG_ERROR(_log) << "BlockCache::AddMicroBlock: VerifyAggSignature failed";
        return false;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::AddMicroBlock: BlockExists";
        return true;
    }

    if (_block_container.AddMicroBlock(block))
    {
        Validate();//TODO optimize: Validate mb first
    }

    //TODO remove after integration tests
    _block_container.DumpCachedBlocks();

    return true;
}

bool BlockCache::AddRequestBlock(RBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Add:R:" << block->CreateTip().to_string();

    if (!_write_q.VerifyAggSignature(block))
    {
        LOG_ERROR(_log) << "BlockCache::AddRequestBlock: VerifyAggSignature failed";
        return false;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::AddRequestBlock: BlockExists";
        return true;
    }

    if (_block_container.AddRequestBlock(block))
    {
        Validate(block->primary_delegate);
    }

    //TODO remove after integration tests
    _block_container.DumpCachedBlocks();

    return true;
}

void BlockCache::StoreEpochBlock(EBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:E:" << block->CreateTip().to_string();

    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::StoreEpochBlock: BlockExists";
        return;
    }

    _write_q.StoreBlock(block);

    if (_block_container.MarkAsValidated(block))
    {
        Validate();
    }

    //TODO remove after integration tests
    _block_container.DumpCachedBlocks();
}

void BlockCache::StoreMicroBlock(MBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:M:" << block->CreateTip().to_string();

    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::StoreMicroBlock: BlockExists";
        return;
    }

    _write_q.StoreBlock(block);

    if (_block_container.MarkAsValidated(block))
    {
        Validate();
    }

    //TODO remove after integration tests
    _block_container.DumpCachedBlocks();
}

void BlockCache::StoreRequestBlock(RBPtr block)
{
    LOG_TRACE(_log) << "BlockCache:Store:R:" << block->CreateTip().to_string();

    if (_block_container.BlockExistsAdd(block))
    {
        LOG_DEBUG(_log) << "BlockCache::StoreRequestBlock: BlockExists";
        return;
    }

    _write_q.StoreBlock(block);

    if (_block_container.MarkAsValidated(block))
    {
        Validate();
    }

    //TODO remove after integration tests
    _block_container.DumpCachedBlocks();
}

bool BlockCache::IsBlockCached(const BlockHash &hash)
{
    LOG_TRACE(_log) << "BlockCache::" << __func__ << ":" << hash.to_string();
    return _block_container.IsBlockCached(hash);
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
            LOG_TRACE(_log) << "BlockCache::"<<__func__<<":R:verifying "
                    << block->CreateTip().to_string();

            if ((success = _write_q.VerifyContent(block, &status)))
            {
                _write_q.StoreBlock(block);
                _block_container.MarkAsValidated(block);
            }
            else
            {
                LOG_TRACE(_log) << "BlockCache::Validate RB status: "
                        << ProcessResultToString(status.reason);
                switch (status.reason)
                {
                case logos::process_result::gap_previous:
                case logos::process_result::gap_source:
                    //TODO any other cases that can be considered as gap?
                    _block_container.AddHashDependency(block->previous, ptr);
                    break;
                case logos::process_result::invalid_request:
                    for (uint32_t i = 0; i < block->requests.size(); ++i)
                    {
                        switch (status.requests[i])
                        {
                        case logos::process_result::gap_previous:
                            _block_container.AddHashDependency(block->requests[i]->previous, ptr);
                            break;
                        case logos::process_result::insufficient_balance:
                        case logos::process_result::insufficient_fee:
                        case logos::process_result::insufficient_token_balance:
                        case logos::process_result::insufficient_token_fee:
                        case logos::process_result::insufficient_funds_for_stake:
                            _block_container.AddAccountDependency(block->requests[i]->GetAccount(), ptr);
                            break;
                        default:
                            break;
                        }
                    }
                    break;
                default:
                    //Since the agg-sigs are already verified,
                    //we expect gap-like reasons.
                    //For any other reason, we log them, and investigate.
                    LOG_ERROR(_log) << "BlockCache::Validate RB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block->CreateTip().to_string();
                    //Throw the block out, otherwise it blocks the rest.
                    _block_container.BlockDelete(block->Hash());
                    success = true;
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
            LOG_TRACE(_log) << "BlockCache::"<<__func__<<":M:verifying "
                    << block->CreateTip().to_string();
            if ((success = _write_q.VerifyContent(block, &status)))
            {
                _write_q.StoreBlock(block);
                _block_container.MarkAsValidated(block);
            }
            else
            {
                LOG_TRACE(_log) << "BlockCache::Validate MB status: "
                        << ProcessResultToString(status.reason);
                switch (status.reason)
                {
                case logos::process_result::gap_previous:
                case logos::process_result::gap_source:
                    //TODO any other cases that can be considered as gap?
                    _block_container.AddHashDependency(block->previous, ptr);
                    break;
                case logos::process_result::invalid_request:
                    for(uint32_t i = 0; i < NUM_DELEGATES; ++i)
                    {
                        if (status.requests[i] == logos::process_result::gap_previous)
                        {
                            _block_container.AddHashDependency(block->tips[i].digest, ptr);
                        }
                    }
                    break;
                default:
                    LOG_ERROR(_log) << "BlockCache::Validate MB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block->CreateTip().to_string();
                    _block_container.BlockDelete(block->Hash());
                    success = true;
                    //TODO recall?
                    break;
                }
            }
        }
        else if (ptr.eptr)
        {
            EBPtr &block = ptr.eptr->block;
            ValidationStatus &status = ptr.eptr->status;
            LOG_TRACE(_log) << "BlockCache::"<<__func__<<":E:verifying "
                    << block->CreateTip().to_string();
            if ((success = _write_q.VerifyContent(block, &status)))
            {
                _write_q.StoreBlock(block);
                _block_container.MarkAsValidated(block);
                LOG_INFO(_log) << "BlockCache::Validated EB, block: "
                        << block->CreateTip().to_string();
            }
            else
            {
                LOG_TRACE(_log) << "BlockCache::Validate EB status: "
                        << ProcessResultToString(status.reason);
                switch (status.reason)
                {
                case logos::process_result::gap_previous:
                case logos::process_result::gap_source:
                    //TODO any other cases that can be considered as gap?
                    _block_container.AddHashDependency(block->previous, ptr);
                    break;
                case logos::process_result::invalid_tip:
                    _block_container.AddHashDependency(block->micro_block_tip.digest, ptr);
                    break;
                default:
                    LOG_ERROR(_log) << "BlockCache::Validate EB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block->CreateTip().to_string();
                    _block_container.BlockDelete(block->Hash());
                    success = true;
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
