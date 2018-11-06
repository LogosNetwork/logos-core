#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/batch_block_bulk_pull.hpp>
#include <mutex>

static std::mutex mutex_s;
static PersistenceManager * persistence_manager = nullptr;

bool BatchBlock::Validate(Store & store, BatchStateBlock &message, int delegate_id)
{
    std::lock_guard<std::mutex> lock(mutex_s);
    if(!persistence_manager) {
        persistence_manager = new PersistenceManager(store);
    }
    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        //if(!persistence_manager.Validate(message.blocks[i], delegate_id))
        if(!persistence_manager->Validate(message.blocks[i]))
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

// RGD:
// Discuss this with Devon
// References ConsensusConnection::Validate<PrePrepareMessage>
// in consensus_connection.cpp
bool BatchBlock::Validate(ConsensusContainer &manager, BatchStateBlock &message, int delegate_id)
{
    PersistenceManager &persistence_manager = manager.get_batch_manager().get_persistence_manager();
    for(uint64_t i = 0; i < message.block_count; ++i)
    {   
        //if(!persistence_manager.Validate(message.blocks[i], delegate_id))
        if(!persistence_manager.Validate(message.blocks[i]))
        {
            return false;
        }
    }
    return true;
}

// RGD:
// Discuss this with Devon
// References PersistenceManager::ApplyUpdates
// in persistence_manager.cpp
void BatchBlock::ApplyUpdates(ConsensusContainer &manager, const BatchStateBlock & message, uint8_t delegate_id)
{
    PersistenceManager &persistence_manager = manager.get_batch_manager().get_persistence_manager();
    persistence_manager.ApplyUpdates(message,delegate_id);
}

BlockHash BatchBlock::getNextBatchStateBlock(Store &store, int delegate, BlockHash &hash) // TODOFUNC
{
    BatchStateBlock batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    return batch.next; // FIXME Correct to use next pointer...
}

std::shared_ptr<BatchStateBlock> BatchBlock::readBatchStateBlock(Store &store, BlockHash &hash)
{
    BatchStateBlock *tmp = new BatchStateBlock();
    std::shared_ptr<BatchStateBlock> block(tmp);
    store.batch_block_get(hash, *block);
    return block;
}
