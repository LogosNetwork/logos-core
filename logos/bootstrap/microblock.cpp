#include <logos/bootstrap/microblock.hpp>

namespace Bootstrap
{
//
//BlockHash getBatchBlockTip(Store& store, int delegate)
//{
//    BlockHash hash = 0;
//    if(!store.batch_tip_get(delegate, hash)) {
//        return hash;
//    }
//    return hash;
//}
//
//uint64_t  getBatchBlockSeqNr(Store& store, int delegate)
//{
//    // The below code will dump core in bsb dtor.
//    // Also, the sequence number is not correct. Need real world database to test.
//    ApprovedBSB batch;
//    BlockHash hash = getBatchBlockTip(store,delegate);
//    if(hash.is_zero()) {
//        return NOT_FOUND;
//    }
//    if(store.batch_block_get(hash, batch)) {
//        return NOT_FOUND;
//    }
//    return batch.sequence;
//}
//
//BlockHash getMicroBlockTip(Store& s)
//{
//    BlockHash hash;
//    if(!s.micro_block_tip_get(hash)) {
//        return hash;
//    }
//    return BlockHash();
//}
//
//uint64_t  getMicroBlockSeqNr(Store& s)
//{
//    BlockHash hash = getMicroBlockTip(s);
//    std::shared_ptr<ApprovedMB> tip = readMicroBlock(s,hash);
//    return tip->sequence;
//}
//
//uint64_t  getMicroBlockSeqNr(Store& s, BlockHash& hash)
//{
//    std::shared_ptr<ApprovedMB> tip = readMicroBlock(s,hash);
//    return tip->sequence;
//}
//
//BlockHash getNextMicroBlock(Store &store, BlockHash &hash)
//{
//	ApprovedMB micro;
//    if(hash.is_zero()) {
//        return hash;
//    }
//    store.micro_block_get(hash, micro);
//    return micro.next;
//}
//
//BlockHash getPrevMicroBlock(Store &store, BlockHash &hash)
//{
//	ApprovedMB micro;
//    if(hash.is_zero()) {
//        return hash;
//    }
//    store.micro_block_get(hash, micro);
//    return micro.previous;
//}
//
//std::shared_ptr<ApprovedMB> readMicroBlock(Store &store, BlockHash &hash)
//{
//    std::shared_ptr<ApprovedMB> micro(new ApprovedMB);
//    if(!store.micro_block_get(hash,*micro)) {
//        return micro;
//    }
//    return nullptr;
//}
//
//void dumpMicroBlockTips(Store &store, BlockHash &hash)
//{
//#ifdef _DEBUG
//    std::shared_ptr<ApprovedMB> micro = readMicroBlock(store,hash);
//    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
//        std::cout << "dumpMicroBlockTips: " << micro->tips[i].to_string() << std::endl;
//    }
//#endif
//}
//
//BlockHash getEpochBlockTip(Store& s)
//{
//    BlockHash hash;
//    if(!s.epoch_tip_get(hash)) {
//        return hash;
//    }
//    return BlockHash();
//}
//
//uint64_t  getEpochBlockSeqNr(Store& s)
//{
//    BlockHash hash = getEpochBlockTip(s);
//    std::shared_ptr<ApprovedEB> tip = readEpochBlock(s,hash);
//    return tip->epoch_number;
//}
//
//uint64_t  getEpochBlockSeqNr(Store& s, BlockHash& hash)
//{
//    std::shared_ptr<ApprovedEB> tip = readEpochBlock(s,hash);
//    return tip->epoch_number;
//}
//
//BlockHash getNextEpochBlock(Store &store, BlockHash &hash)
//{
//    ApprovedEB epoch;
//    if(hash.is_zero()) {
//        return hash;
//    }
//    store.epoch_get(hash, epoch);
//    return epoch.next;
//}
//
//BlockHash getPrevEpochBlock(Store &store, BlockHash &hash)
//{
//    ApprovedEB epoch;
//    if(hash.is_zero()) {
//        return hash;
//    }
//    store.epoch_get(hash, epoch);
//    return epoch.previous;
//}
//
//std::shared_ptr<ApprovedEB> readEpochBlock(Store &store, BlockHash &hash)
//{
//    std::shared_ptr<ApprovedEB> epoch(new ApprovedEB);
//    if(!store.epoch_get(hash,*epoch)) {
//        return epoch;
//    }
//    return nullptr;
//}

}
/*

#include <mutex>


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

bool Validate(Store & store, const ApprovedBSB & message, int delegate_id, ValidationStatus * status)
{
    std::lock_guard<std::mutex> lock(mutex_s);
    return get_persistence_manager(store)->Validate(message, status);
}

void ApplyUpdates(Store & store, const ApprovedBSB & message, uint8_t delegate_id)
{
    std::lock_guard<std::mutex> lock(mutex_s);
	//    if(!persistence_manager) {
	//        persistence_manager = new PersistenceManager<BSBCT>(store,nullptr);
	//    }
    get_persistence_manager(store)->ApplyUpdates(message, delegate_id);
}

BlockHash getNextBatchStateBlock(Store &store, int delegate, BlockHash &hash)
{
    ApprovedBSB batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    return batch.next;
}

BlockHash getPrevBatchStateBlock(Store &store, int delegate, BlockHash &hash)
{
    ApprovedBSB batch;
    if(hash.is_zero()) {
        return hash;
    }
    store.batch_block_get(hash, batch);
    return batch.previous;
}


std::shared_ptr<ApprovedBSB> readBatchStateBlock(Store &store, BlockHash &hash)
{
    logos::transaction transaction (store.environment, nullptr, false);
    ApprovedBSB *tmp = new ApprovedBSB();
    std::shared_ptr<ApprovedBSB> block(tmp);
    if(store.batch_block_get(hash, *tmp, transaction)) {
        return block;
    }
    return block;
}
*/
