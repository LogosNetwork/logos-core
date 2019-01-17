#include <logos/blockstore.hpp>
#include <logos/bootstrap/bulk_pull_response.hpp>
#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/common.hpp>

#include <mutex>

#define _DEBUG 1

static std::mutex mutex_s;
static PersistenceManager<BSBCT> * persistence_manager = nullptr;

using Request = RequestMessage<ConsensusType::BatchStateBlock>;
using PrePrepare = PrePrepareMessage<BSBCT>;

bool BatchBlock::Validate(Store & store, const BatchStateBlock & message, int delegate_id)
{
    std::lock_guard<std::mutex> lock(mutex_s);
    if(!persistence_manager) {
        persistence_manager = new PersistenceManager<BSBCT>(store,nullptr);
    }

    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        if(!persistence_manager->Validate(static_cast<const Request&>(message.blocks[i])))
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
        persistence_manager = new PersistenceManager<BSBCT>(store,nullptr);
    }
    persistence_manager->ApplyUpdates(static_cast<const PrePrepare&>(message),delegate_id);
}

BlockHash BatchBlock::getNextBatchStateBlock(Store &store, int delegate, BlockHash &hash)
{
    BatchStateBlock batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    return batch.next;
}

BlockHash BatchBlock::getPrevBatchStateBlock(Store &store, int delegate, BlockHash &hash)
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
