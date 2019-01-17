#include <logos/blockstore.hpp>
#include <logos/bootstrap/bulk_pull_response.hpp>
#include <logos/consensus/persistence/batchblock/nondel_batchblock_persistence.hpp>//batchblock_persistence.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/common.hpp>

#include <mutex>

#define _DEBUG 1

static std::mutex mutex_s;
static NonDelPersistenceManager<BSBCT> * persistence_manager = nullptr;
static NonDelPersistenceManager<BSBCT> * get_persistence_manager(logos::block_store & store)
{
    ///std::lock_guard<std::mutex> lock(mutex_s);
    if(!persistence_manager) {
        persistence_manager = new NonDelPersistenceManager<BSBCT>(store);
    }

    return persistence_manager;
}
//static PersistenceManager<BSBCT> * persistence_manager = nullptr;
//
//using Request = RequestMessage<ConsensusType::ApprovedBSB>;
//using PrePrepare = PrePrepareMessage<BSBCT>;

bool BatchBlock::Validate(Store & store, const ApprovedBSB & message, int delegate_id)
{
    std::lock_guard<std::mutex> lock(mutex_s);
    ValidationStatus status;//TODO check return from validate
    return get_persistence_manager(store)->Validate(message, &status);

}

void BatchBlock::ApplyUpdates(Store & store, const ApprovedBSB & message, uint8_t delegate_id)
{
    std::lock_guard<std::mutex> lock(mutex_s);
	//    if(!persistence_manager) {
	//        persistence_manager = new PersistenceManager<BSBCT>(store,nullptr);
	//    }
    get_persistence_manager(store)->ApplyUpdates(message, delegate_id);
}

BlockHash BatchBlock::getNextBatchStateBlock(Store &store, int delegate, BlockHash &hash)
{
    ApprovedBSB batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    return batch.next;
}

BlockHash BatchBlock::getPrevBatchStateBlock(Store &store, int delegate, BlockHash &hash)
{
    ApprovedBSB batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    return batch.previous;
}


std::shared_ptr<ApprovedBSB> BatchBlock::readBatchStateBlock(Store &store, BlockHash &hash)
{
    logos::transaction transaction (store.environment, nullptr, false);
    ApprovedBSB *tmp = new ApprovedBSB();
    std::shared_ptr<ApprovedBSB> block(tmp);
    if(store.batch_block_get(hash, *tmp, transaction)) {
        return block;
    }
    return block;
}
