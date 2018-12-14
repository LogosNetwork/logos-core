#include <logos/bootstrap/epoch.hpp>

BlockHash EpochBlock::getEpochBlockTip(Store& s) // TODOFUNC
{
    BlockHash hash;
    if(!s.epoch_tip_get(hash)) {
        return hash;
    }
    return BlockHash();
}

uint64_t  EpochBlock::getEpochBlockSeqNr(Store& s) // TODOFUNC
{
    BlockHash hash = EpochBlock::getEpochBlockTip(s);
    std::shared_ptr<Epoch> tip = EpochBlock::readEpochBlock(s,hash);
    return tip->epoch_number;
}

BlockHash EpochBlock::getNextEpochBlock(Store &store, BlockHash &hash) // TODOFUNC
{
    Epoch epoch;
    if(hash.is_zero()) {
        return hash;
    }
    store.epoch_get(hash, epoch);
    return epoch.next;
}

BlockHash EpochBlock::getPrevEpochBlock(Store &store, BlockHash &hash) // TODOFUNC
{
    Epoch epoch;
    if(hash.is_zero()) {
        return hash;
    }
    store.epoch_get(hash, epoch);
    return epoch.previous;
}

std::shared_ptr<Epoch> EpochBlock::readEpochBlock(Store &store, BlockHash &hash)
{
    std::shared_ptr<Epoch> epoch(new Epoch);
    if(!store.epoch_get(hash,*epoch)) {
        return epoch;
    }
    return nullptr;
}
