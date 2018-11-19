#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/batch_block_bulk_pull.hpp>
#include <mutex>

#define _DEBUG 1

static std::mutex mutex_s;
static PersistenceManager * persistence_manager = nullptr;

bool BatchBlock::Validate(Store & store, BatchStateBlock &message, int delegate_id)
{
    std::lock_guard<std::mutex> lock(mutex_s);
    if(!persistence_manager) {
        persistence_manager = new PersistenceManager(store);
    }

#ifdef _DEBUG
    std::cout << "message.block_count: " << message.block_count << std::endl;
#endif

#if 0 // block_count == 0 => account has no sends in its history.
    if(message.block_count <= 0) {
        return false;
    }
#endif

    for(uint64_t i = 0; i < message.block_count; ++i)
    {
        //if(!persistence_manager.Validate(message.blocks[i], delegate_id))
        logos::process_return rtvl;   
        if(!persistence_manager->Validate(message.blocks[i],rtvl))
        {
            std::cout << "persistence_manager: failed: code: " << (int)rtvl.code << std::endl;
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
#if 0
    PersistenceManager &persistence_manager = manager.get_batch_manager().get_persistence_manager();
    for(uint64_t i = 0; i < message.block_count; ++i)
    {   
        //if(!persistence_manager.Validate(message.blocks[i], delegate_id))
        if(!persistence_manager.Validate(message.blocks[i]))
        {
            return false;
        }
    }
#endif
    return true;
}

// RGD:
// Discuss this with Devon
// References PersistenceManager::ApplyUpdates
// in persistence_manager.cpp
void BatchBlock::ApplyUpdates(ConsensusContainer &manager, const BatchStateBlock & message, uint8_t delegate_id)
{
#if 0
    PersistenceManager &persistence_manager = manager.get_batch_manager().get_persistence_manager();
    persistence_manager.ApplyUpdates(message,delegate_id);
#endif
}

BlockHash BatchBlock::getNextBatchStateBlock(Store &store, int delegate, BlockHash &hash) // TODOFUNC
{
    BatchStateBlock batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    std::cout << "BatchBlock::getNextBatchStateBlock: " << batch.next.to_string() << std::endl;
    return batch.next; // FIXME Correct to use next pointer...
}

BlockHash BatchBlock::getPrevBatchStateBlock(Store &store, int delegate, BlockHash &hash) // TODOFUNC
{
    BatchStateBlock batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    std::cout << "BatchBlock::getPrevBatchStateBlock: prev: " << batch.previous.to_string() << " delegate: " << delegate << " next: " << batch.next.to_string() << std::endl;
    return batch.previous;
}


std::shared_ptr<BatchStateBlock> BatchBlock::readBatchStateBlock(Store &store, BlockHash &hash)
{
    logos::transaction transaction (store.environment, nullptr, false);
    BatchStateBlock *tmp = new BatchStateBlock();
    std::shared_ptr<BatchStateBlock> block(tmp);
    std::cout << "BatchBlock::readBatchStateBlock: " << sizeof(*tmp) << " pointer: " << (uint64_t)tmp << std::endl;
    if(store.batch_block_get(hash, block, transaction)) {
    //if(store.batch_block_get(hash, *tmp)) { // FIXME
        std::cout << "BatchBlock::readBatchStateBlock: error " << std::endl;
        return block;
    }
    return block;
}
