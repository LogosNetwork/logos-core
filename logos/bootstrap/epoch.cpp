#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/epoch.hpp>

BlockHash EpochBlock::getEpochBlockTip(Store& s, int delegate) // TODOFUNC
{
    BlockHash hash;
    if(!s.epoch_tip_get(hash)) {
        return hash;
    }
    return BlockHash();
}

uint64_t  EpochBlock::getEpochBlockSeqNr(Store& s, int delegate) // TODOFUNC
{
#ifdef _DEBUG
    return 0;
#else
    BlockHash hash = BatchBlock::getMicroBlockTip(s,delegate);
    std::shared_ptr<Epoch> tip = EpochBlock::readEpochBlock(s,hash);
    return tip->_epoch_number;
#endif
}

BlockHash EpochBlock::getNextEpochBlock(Store &store, int delegate, BlockHash &hash) // TODOFUNC
{
    Epoch epoch;
    if(hash.is_zero()) {
        return hash;
    }
    store.epoch_get(hash, epoch); // TESTING previous
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
