#include "block_cache.hpp"

namespace logos {

BlockCache::BlockCache(Store &store)
    : store_(store)
    , eb_handler(store)
    , mb_handler(store)
    , rb_handler(store)
{
}

bool BlockCache::AddEpochBlock(EBPtr block)
{
    LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();
    if(!eb_handler.VerifyAggSignature(*block))
    {
        LOG_TRACE(log) << "BlockCache::AddEB: VerifyAggSignature failed";
        return false;
    }

    std::lock_guard<std::mutex> lck (mtx);
    //safe to ignore the block for both p2p and bootstrap
    if(eb_handler.BlockExists(*block))
    {
        LOG_TRACE(log) << "BlockCache::AddEB: BlockExists";
        return true;
    }

    bool found = false;
    for(auto bi = block_container.epochs.rbegin(); bi != block_container.epochs.rend(); ++ bi)
    {
        if(bi->epoch_num == block->epoch_number)
        {
            //duplicate
            if(bi->eb == nullptr)
            {
                bi->eb = block;
                block_container.cached_blocks.insert(block->Hash());
            }
            found = true;
            break;
        }
        else if(bi->epoch_num < block->epoch_number)
        {
            block_container.epochs.emplace(bi.base(), PendingBlockContainer::EpochPeriod(block));
            block_container.cached_blocks.insert(block->Hash());
            found = true;
            break;
        }
    }
    if(!found)
    {
        block_container.epochs.emplace_front(PendingBlockContainer::EpochPeriod(block));
        block_container.cached_blocks.insert(block->Hash());
        Validate();//TODO optimize: Validate eb first
    }

    //TODO remove after integration tests
    LOG_TRACE(log) << "BlockCache::"<<__func__<<": cached hashes: " << block_container.cached_blocks.size();
    for(auto & h : block_container.cached_blocks)
        LOG_TRACE(log) << h.to_string();

    return true;
}

bool BlockCache::AddMicroBlock(MBPtr block)
{
    LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();
    if(!mb_handler.VerifyAggSignature(*block))
    {
        LOG_TRACE(log) << "BlockCache::AddMB: VerifyAggSignature failed";
        return false;
    }

    std::lock_guard<std::mutex> lck (mtx);
    //safe to ignore the block for both p2p and bootstrap
    if(mb_handler.BlockExists(*block))
    {
        LOG_TRACE(log) << "BlockCache::AddMB: BlockExists";
        return true;
    }

    bool found = false;
    bool add2begin = false;
    for(auto bi = block_container.epochs.rbegin(); bi != block_container.epochs.rend(); ++ bi)
    {
        if(bi->epoch_num == block->epoch_number)
        {
            for(auto mbi = bi->mbs.rbegin(); mbi != bi->mbs.rend(); ++ mbi)
            {
                if((*mbi)->sequence == block->sequence)
                {
                    //duplicate
                    found = true;
                    break;
                }
                else if ((*mbi)->sequence < block->sequence)
                {
                    bi->mbs.emplace(mbi.base(), block);
                    block_container.cached_blocks.insert(block->Hash());

                    found = true;
                    break;
                }
            }
            if(!found)
            {
                bi->mbs.emplace_front(block);
                block_container.cached_blocks.insert(block->Hash());

                found = true;
                auto temp_i = bi;
                add2begin = ++temp_i == block_container.epochs.rend();
            }
            break;
        }
        else if(bi->epoch_num < block->epoch_number)
        {
            block_container.epochs.emplace(bi.base(), PendingBlockContainer::EpochPeriod(block));
            add2begin = block_container.cached_blocks.empty();
            block_container.cached_blocks.insert(block->Hash());
            found = true;
            break;
        }
    }
    if(!found)
    {
        block_container.epochs.emplace_front(PendingBlockContainer::EpochPeriod(block));
        block_container.cached_blocks.insert(block->Hash());
        found = true;
        add2begin = true;
    }
    if(add2begin)
    {
        Validate();//TODO optimize: Validate mb first
    }

    //TODO remove after integration tests
    LOG_TRACE(log) << "BlockCache::"<<__func__<<": cached hashes: " << block_container.cached_blocks.size();
    for(auto & h : block_container.cached_blocks)
        LOG_TRACE(log) << h.to_string();

    return true;
}

bool BlockCache::AddRequestBlock(RBPtr block)
{
    LOG_TRACE(log) << "BlockCache::" << __func__ <<":" << block->CreateTip().to_string();

    if(!rb_handler.VerifyAggSignature(*block))
    {
        LOG_TRACE(log) << "BlockCache::AddBSB: VerifyAggSignature failed";
        return false;
    }

    std::lock_guard<std::mutex> lck (mtx);
    //safe to ignore the block for both p2p and bootstrap
    if(rb_handler.BlockExists(*block))
    {
        LOG_TRACE(log) << "BlockCache::AddMB: BlockExists";
        return true;
    }

    bool found = false;
    bool add2begin = false;
    for(auto bi = block_container.epochs.rbegin(); bi != block_container.epochs.rend(); ++ bi)
    {
        if(bi->epoch_num == block->epoch_number)
        {
            for(auto rbi = bi->rbs[block->primary_delegate].rbegin();
                    rbi != bi->rbs[block->primary_delegate].rend(); ++ rbi)
            {
                if((*rbi)->sequence == block->sequence)
                {
                    //duplicate
                    found = true;
                    break;
                }
                else if ((*rbi)->sequence < block->sequence)
                {
                    bi->rbs[block->primary_delegate].emplace(rbi.base(), block);
                    block_container.cached_blocks.insert(block->Hash());

                    found = true;
                    break;
                }
            }
            if(!found)
            {
                bi->rbs[block->primary_delegate].emplace_front(block);
                block_container.cached_blocks.insert(block->Hash());

                found = true;
                add2begin = true;
            }
            break;
        }
        else if(bi->epoch_num < block->epoch_number)
        {
            block_container.epochs.emplace(bi.base(), PendingBlockContainer::EpochPeriod(block));
            block_container.cached_blocks.insert(block->Hash());
            found = true;
            add2begin = true;
            break;
        }
    }
    if(!found)
    {
        block_container.epochs.emplace_front(PendingBlockContainer::EpochPeriod(block));
        block_container.cached_blocks.insert(block->Hash());
        found = true;
        add2begin = true;
    }
    if(add2begin)
    {
        Validate(block->primary_delegate);
    }

    //TODO remove after integration tests
    LOG_TRACE(log) << "BlockCache::"<<__func__<<": cached hashes: " << block_container.cached_blocks.size();
    for(auto & h : block_container.cached_blocks)
        LOG_TRACE(log) << h.to_string();

    return true;
}

void BlockCache::StoreEpochBlock(EBPtr block)
{
    eb_handler.ApplyUpdates(*block, block->primary_delegate);
}

void BlockCache::StoreMicroBlock(MBPtr block)
{
    mb_handler.ApplyUpdates(*block, block->primary_delegate);
}

void BlockCache::StoreRequestBlock(RBPtr block)
{
    rb_handler.ApplyUpdates(*block, block->primary_delegate);
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
    auto e = block_container.epochs.begin();
    while( e != block_container.epochs.end())
    {
        uint num_rb_chain_no_progress = 0;
        // try rb chains till num_rb_chain_no_progress reaches NUM_DELEGATES
        while(num_rb_chain_no_progress < NUM_DELEGATES)
        {
            for(;;)
            {
                std::list<RBPtr>::iterator to_validate = e->rbs[rb_idx].begin();
                if(to_validate == e->rbs[rb_idx].end())
                {
                    //cannot make progress with empty list
                    num_rb_chain_no_progress++;
                    rb_idx = (rb_idx+1)%NUM_DELEGATES;
                    break;//for(;;)
                }
                else
                {
                    ApprovedRB & block = *(*to_validate);
                    ValidationStatus status;
                    LOG_TRACE(log) << "BlockCache::"<<__func__<<": verifying "
                            << block.CreateTip().to_string();

                    if(rb_handler.VerifyContent(block, &status))
                    {
                        rb_handler.ApplyUpdates(block, block.primary_delegate);
                        block_container.cached_blocks.erase(block.Hash());
                        e->rbs[rb_idx].pop_front();
                        num_rb_chain_no_progress = 0;
                    }
                    else
                    {
                        LOG_TRACE(log) << "BlockCache::Validate BSB status: "
                                << ProcessResultToString(status.reason);
    #ifdef REASON_CLEARED
                        switch (status.reason) {
                            case logos::process_result::gap_previous:
                            case logos::process_result::gap_source:
                                //TODO any other cases that can be considered as gap?
                                num_rb_chain_no_progress++;
                                rb_idx = (rb_idx+1)%NUM_DELEGATES;
                                break;
                            default:
                                //Since the agg-sigs are already verified,
                                //we expect gap-like reasons.
                                //For any other reason, we log them, and investigate.
                                LOG_ERROR(log) << "BlockCache::Validate BSB status: "
                                        << ProcessResultToString(status.reason)
                                        << " block " << block.CreateTip().to_string();
                                //Throw the block out, otherwise it blocks the rest.
                                block_container.cached_blocks.erase(block.Hash());
                                e->rbs[rb_idx].pop_front();
                                //TODO recall?
                                //TODO detect double spend?
                                break;
                        }
    #else
                        num_rb_chain_no_progress++;
                        rb_idx = (rb_idx+1)%NUM_DELEGATES;
    #endif
                        break;//for(;;)
                    }
                }
            }
        }

        bool mbs_empty = e->mbs.empty();
        bool last_mb = false;
        while(!e->mbs.empty())
        {
            ApprovedMB & block = *(e->mbs.front());
            ValidationStatus status;
            if(mb_handler.VerifyContent(block, &status))
            {
                mb_handler.ApplyUpdates(block, block.primary_delegate);
                last_mb = block.last_micro_block;
                block_container.cached_blocks.erase(block.Hash());
                e->mbs.pop_front();
                if(last_mb)
                    assert(e->mbs.empty());
            }
            else
            {
                LOG_TRACE(log) << "BlockCache::Validate MB status: "
                        << ProcessResultToString(status.reason);
#ifdef REASON_CLEARED
                switch (status.reason) {
                    case logos::process_result::gap_previous:
                    case logos::process_result::gap_source:
                        //TODO any other cases that can be considered as gap?
                        break;
                    default:
                        LOG_ERROR(log) << "BlockCache::Validate MB status: "
                            << ProcessResultToString(status.reason)
                            << " block " << block.CreateTip().to_string();
                        block_container.cached_blocks.erase(block.Hash());
                        e->mbs.pop_front();
                        //TODO recall?
                        break;
                }
#endif
                break;
            }
        }

        bool e_finished = false;
        if(last_mb || mbs_empty)
        {
            if( e->eb != nullptr)
            {
                ApprovedEB & block = *e->eb;
                ValidationStatus status;
                if(eb_handler.VerifyContent(block, &status))
                {
                    eb_handler.ApplyUpdates(block, block.primary_delegate);
                    LOG_INFO(log) << "BlockCache::Validated EB, block: "
                                  << block.CreateTip().to_string();
                    block_container.cached_blocks.erase(block.Hash());
                    block_container.epochs.erase(e);
                    e_finished = true;
                }
                else
                {
                    LOG_TRACE(log) << "BlockCache::Validate EB status: "
                            << ProcessResultToString(status.reason);
    #ifdef REASON_CLEARED
                    switch (status.reason) {
                        case logos::process_result::gap_previous:
                        case logos::process_result::gap_source:
                            //TODO any other cases that can be considered as gap?
                            break;
                        default:
                            LOG_ERROR(log) << "BlockCache::Validate EB status: "
                                << ProcessResultToString(status.reason)
                                << " block " << block.CreateTip().to_string();
                            block_container.cached_blocks.erase(block.Hash());
                            block_container.epochs.pop_front();
                            //TODO recall?
                            break;
                    }
    #endif
                }
            }
            else
            {
                LOG_INFO(log) << "BlockCache::Validated, no MB, no EB, e#=" << e->epoch_num;
            }
        }

        if(e_finished)
        {
            e = block_container.epochs.begin();
        }
        else
        {
            //two-tip case, i.e. first 10 minutes of the latest epoch
            bool last_epoch_begin = block_container.epochs.size()==2 &&
                    e->eb == nullptr &&
                    e->mbs.empty() &&
                    (++e)->mbs.empty();
            if(!last_epoch_begin)
            {
                e = block_container.epochs.end();
            }
            else
            {
                //already did ++e
            }
        }
    }
    LOG_ERROR(log) << "BlockCache::"<<__func__<<"}";
}

}
