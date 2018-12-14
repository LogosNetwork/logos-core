#include <logos/bootstrap/batch_block_bulk_pull.hpp>
#include <mutex>

#define _DEBUG 1

static std::mutex mutex_s;
static PersistenceManager * persistence_manager = nullptr;

bool BatchBlock::Validate(Store & store, const BatchStateBlock & message, int delegate_id)
{
    std::lock_guard<std::mutex> lock(mutex_s);
    if(!persistence_manager) {
        persistence_manager = new PersistenceManager(store);
    }

    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        logos::process_return rtvl;   
        if(!persistence_manager->Validate(message.blocks[i],rtvl))
        {
            return false;
        }
    }
    return true;
}

void BatchBlock::ApplyUpdates(Store & store, const BatchStateBlock & message, uint8_t delegate_id)
{
    std::lock_guard<std::mutex> lock(mutex_s);
    if(!persistence_manager) {
        persistence_manager = new PersistenceManager(store);
    }
    persistence_manager->ApplyUpdates(message,delegate_id);
}

BlockHash BatchBlock::getNextBatchStateBlock(Store &store, int delegate, BlockHash &hash) // TODOFUNC
{
    BatchStateBlock batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    return batch.next;
}

BlockHash BatchBlock::getPrevBatchStateBlock(Store &store, int delegate, BlockHash &hash) // TODOFUNC
{
    BatchStateBlock batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    return batch.previous;
}


std::shared_ptr<BatchStateBlock> BatchBlock::readBatchStateBlock(Store &store, BlockHash &hash)
{
    logos::transaction transaction (store.environment, nullptr, false);
    BatchStateBlock *tmp = new BatchStateBlock();
    std::shared_ptr<BatchStateBlock> block(tmp);
    if(store.batch_block_get(hash, *tmp, transaction)) {
        return block;
    }
    return block;
}
