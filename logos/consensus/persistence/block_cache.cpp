#include "block_cache.hpp"

namespace logos {

BlockCache::BlockCache(Store &store)
    : store_(store)
    , write_q(store)
    , block_container(write_q)
{
}

bool BlockCache::AddEpochBlock(EBPtr block)
{
    LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();

    if (!write_q.VerifyAggSignature(block))
    {
        LOG_TRACE(log) << "BlockCache::AddEpochBlock: VerifyAggSignature failed";
        return false;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (block_container.BlockExists(block))
    {
        LOG_TRACE(log) << "BlockCache::AAddEpochBlock: BlockExists";
        return true;
    }

    if (block_container.AddEpochBlock(block))
    {
        Validate();//TODO optimize: Validate eb first
    }

    //TODO remove after integration tests
    block_container.DumpCachedBlocks();

    return true;
}

bool BlockCache::AddMicroBlock(MBPtr block)
{
    LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();
    if (!write_q.VerifyAggSignature(block))
    {
        LOG_TRACE(log) << "BlockCache::AddMicroBlock: VerifyAggSignature failed";
        return false;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (block_container.BlockExists(block))
    {
        LOG_TRACE(log) << "BlockCache::AddMicroBlock: BlockExists";
        return true;
    }

    if (block_container.AddMicroBlock(block))
    {
	Validate();//TODO optimize: Validate mb first
    }

    //TODO remove after integration tests
    block_container.DumpCachedBlocks();

    return true;
}

bool BlockCache::AddRequestBlock(RBPtr block)
{
    LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();

    if (!write_q.VerifyAggSignature(block))
    {
        LOG_TRACE(log) << "BlockCache::AddRequestBlock: VerifyAggSignature failed";
        return false;
    }

    //safe to ignore the block for both p2p and bootstrap
    if (block_container.BlockExists(block))
    {
        LOG_TRACE(log) << "BlockCache::AddRequestBlock: BlockExists";
        return true;
    }

    if (block_container.AddRequestBlock(block))
    {
        Validate(block->primary_delegate);
    }

    //TODO remove after integration tests
    block_container.DumpCachedBlocks();

    return true;
}

void BlockCache::StoreEpochBlock(EBPtr block)
{
    if (block_container.BlockExists(block))
    {
        LOG_TRACE(log) << "BlockCache::StoreEpochBlock: BlockExists";
        return;
    }
    write_q.StoreBlock(block);
    if (block_container.MarkAsValidated(block))
        Validate();
}

void BlockCache::StoreMicroBlock(MBPtr block)
{
    if (block_container.BlockExists(block))
    {
        LOG_TRACE(log) << "BlockCache::StoreMicroBlock: BlockExists";
        return;
    }
    write_q.StoreBlock(block);
    if (block_container.MarkAsValidated(block))
        Validate();
}

void BlockCache::StoreRequestBlock(RBPtr block)
{
    if (block_container.BlockExists(block))
    {
        LOG_TRACE(log) << "BlockCache::StoreRequestBlock: BlockExists";
        return;
    }
    write_q.StoreBlock(block);
    if (block_container.MarkAsValidated(block))
        Validate();
}

bool BlockCache::IsBlockCached(const BlockHash & b)
{
    LOG_TRACE(log) << "BlockCache::" << __func__ << ":" << b.to_string();
    std::lock_guard<std::mutex> lck (mtx);
    return block_container.cached_blocks.find(b) != block_container.cached_blocks.end();
}

void BlockCache::Validate(uint8_t rb_idx)
{
    LOG_TRACE(log) << "BlockCache::"<<__func__<<"{";
    assert(rb_idx<=NUM_DELEGATES);

    PendingBlockContainer::ChainPtr ptr;
    bool success = false;

    while (block_container.GetNextBlock(ptr, rb_idx, success))
    {
        if (ptr.rptr)
        {
            RBPtr &block = ptr.rptr->block;
            ValidationStatus &status = ptr.rptr->status;
            LOG_TRACE(log) << "BlockCache::"<<__func__<<": verifying "
                            << block->CreateTip().to_string();

            if ((success = write_q.VerifyContent(block, &status)))
            {
                write_q.StoreBlock(block);
                block_container.MarkAsValidated(block);
            }
            else
            {
                LOG_TRACE(log) << "BlockCache::Validate RB status: "
                                << ProcessResultToString(status.reason);
                switch (status.reason) {
                case logos::process_result::gap_previous:
                case logos::process_result::gap_source:
                    //TODO any other cases that can be considered as gap?
                    block_container.AddDependency(block->previous, ptr.rptr);
                    break;
                case logos::process_result::invalid_request:
                    for (uint32_t i = 0; i < block->requests.size(); ++i)
                    {
                        if (status.requests[i] == logos::process_result::gap_previous)
                        {
                            block_container.AddDependency(block->requests[i]->previous, ptr.rptr);
                        }
                    }
                    break;
                default:
                    //Since the agg-sigs are already verified,
                    //we expect gap-like reasons.
                    //For any other reason, we log them, and investigate.
                    LOG_ERROR(log) << "BlockCache::Validate RB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block->CreateTip().to_string();
                    //Throw the block out, otherwise it blocks the rest.
                    block_container.cached_blocks.erase(block->Hash());
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
            if ((success = write_q.VerifyContent(block, &status)))
            {
                write_q.StoreBlock(block);
                block_container.MarkAsValidated(block);
            }
            else
            {
                LOG_TRACE(log) << "BlockCache::Validate MB status: "
                        << ProcessResultToString(status.reason);
                switch (status.reason) {
                case logos::process_result::gap_previous:
                case logos::process_result::gap_source:
                    //TODO any other cases that can be considered as gap?
                    block_container.AddDependency(block->previous, ptr.mptr);
                    break;
                case logos::process_result::invalid_request:
                    for(uint32_t i = 0; i < NUM_DELEGATES; ++i)
                    {
                        if (status.requests[i] == logos::process_result::gap_previous)
                        {
                            block_container.AddDependency(block->tips[i].digest, ptr.mptr);
                        }
                    }
                    break;
                default:
                    LOG_ERROR(log) << "BlockCache::Validate MB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block->CreateTip().to_string();
                    block_container.cached_blocks.erase(block->Hash());
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
            if ((success = write_q.VerifyContent(block, &status)))
            {
                write_q.StoreBlock(block);
                block_container.MarkAsValidated(block);
                LOG_INFO(log) << "BlockCache::Validated EB, block: "
                              << block->CreateTip().to_string();
            }
            else
            {
                LOG_TRACE(log) << "BlockCache::Validate EB status: "
                        << ProcessResultToString(status.reason);
                switch (status.reason) {
                case logos::process_result::gap_previous:
                case logos::process_result::gap_source:
                    //TODO any other cases that can be considered as gap?
                    block_container.AddDependency(block->previous, ptr.eptr);
                    break;
                case logos::process_result::invalid_tip:
                    block_container.AddDependency(block->micro_block_tip.digest, ptr.eptr);
                    break;
                default:
                    LOG_ERROR(log) << "BlockCache::Validate EB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block->CreateTip().to_string();
                    block_container.cached_blocks.erase(block->Hash());
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

    LOG_TRACE(log) << "BlockCache::"<<__func__<<"}";
}

}
