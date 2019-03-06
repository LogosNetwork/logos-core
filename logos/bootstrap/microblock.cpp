#include <logos/bootstrap/bulk_pull_response.hpp>
#include <logos/bootstrap/microblock.hpp>

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
