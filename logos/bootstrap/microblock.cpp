#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/pull_connection.hpp>

BlockHash Micro::getMicroBlockTip(Store& s)
{
    BlockHash hash;
    if(!s.micro_block_tip_get(hash)) {
        return hash;
    }
    return BlockHash();
}

uint64_t  Micro::getMicroBlockSeqNr(Store& s)
{
    BlockHash hash = Micro::getMicroBlockTip(s);
    std::shared_ptr<ApprovedMB> tip = Micro::readMicroBlock(s,hash);
    return tip->sequence;
}

uint64_t  Micro::getMicroBlockSeqNr(Store& s, BlockHash& hash)
{
    std::shared_ptr<ApprovedMB> tip = Micro::readMicroBlock(s,hash);
    return tip->sequence;
}

BlockHash Micro::getNextMicroBlock(Store &store, BlockHash &hash)
{
	ApprovedMB micro;
    if(hash.is_zero()) {
        return hash;
    }
    store.micro_block_get(hash, micro);
    return micro.next;
}

BlockHash Micro::getPrevMicroBlock(Store &store, BlockHash &hash)
{
	ApprovedMB micro;
    if(hash.is_zero()) {
        return hash;
    }
    store.micro_block_get(hash, micro);
    return micro.previous;
}

std::shared_ptr<ApprovedMB> Micro::readMicroBlock(Store &store, BlockHash &hash)
{
    std::shared_ptr<ApprovedMB> micro(new ApprovedMB);
    if(!store.micro_block_get(hash,*micro)) {
        return micro;
    }
    return nullptr;
}

void Micro::dumpMicroBlockTips(Store &store, BlockHash &hash)
{
#ifdef _DEBUG
    std::shared_ptr<ApprovedMB> micro = Micro::readMicroBlock(store,hash);
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        std::cout << "Micro::dumpMicroBlockTips: " << micro->tips[i].to_string() << std::endl;
    }
#endif
}

#include <logos/bootstrap/attempt.hpp>

BlockHash EpochBlock::getEpochBlockTip(Store& s)
{
    BlockHash hash;
    if(!s.epoch_tip_get(hash)) {
        return hash;
    }
    return BlockHash();
}

uint64_t  EpochBlock::getEpochBlockSeqNr(Store& s)
{
    BlockHash hash = EpochBlock::getEpochBlockTip(s);
    std::shared_ptr<ApprovedEB> tip = EpochBlock::readEpochBlock(s,hash);
    return tip->epoch_number;
}

uint64_t  EpochBlock::getEpochBlockSeqNr(Store& s, BlockHash& hash)
{
    std::shared_ptr<ApprovedEB> tip = EpochBlock::readEpochBlock(s,hash);
    return tip->epoch_number;
}

BlockHash EpochBlock::getNextEpochBlock(Store &store, BlockHash &hash)
{
    ApprovedEB epoch;
    if(hash.is_zero()) {
        return hash;
    }
    store.epoch_get(hash, epoch);
    return epoch.next;
}

BlockHash EpochBlock::getPrevEpochBlock(Store &store, BlockHash &hash)
{
    ApprovedEB epoch;
    if(hash.is_zero()) {
        return hash;
    }
    store.epoch_get(hash, epoch);
    return epoch.previous;
}

std::shared_ptr<ApprovedEB> EpochBlock::readEpochBlock(Store &store, BlockHash &hash)
{
    std::shared_ptr<ApprovedEB> epoch(new ApprovedEB);
    if(!store.epoch_get(hash,*epoch)) {
        return epoch;
    }
    return nullptr;
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

bool BatchBlock::Validate(Store & store, const ApprovedBSB & message, int delegate_id, ValidationStatus * status)
{
    std::lock_guard<std::mutex> lock(mutex_s);
    return get_persistence_manager(store)->Validate(message, status);
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
*/
