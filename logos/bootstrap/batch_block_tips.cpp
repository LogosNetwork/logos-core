#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/batch_block_tips.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/epoch.hpp>

BlockHash BatchBlock::getBatchBlockTip(Store& store, int delegate)
{
    BlockHash hash;
    if(!store.batch_tip_get(delegate,hash)) {
        return hash;
    }
    return BlockHash();
}

uint64_t  BatchBlock::getBatchBlockSeqNr(Store& store, int delegate) // TODOFUNC
{
    // The below code will dump core in bsb dtor.
    // Also, the sequence number is not correct. Need real world database to test.
    BatchStateBlock *batch = new BatchStateBlock();
    BlockHash hash = BatchBlock::getBatchBlockTip(store,delegate);
    if(hash.is_zero()) {
        std::cout << "BatchBlock::getBatchBlockSeqNr: " << __LINE__ << std::endl;
        return -1; // FIXME is this the correct return value on error ?
    }
    std::cout << "BatchBlock::getBatchBlockSeqNr: " << __LINE__ << std::endl;
    if(store.batch_block_get(hash, *batch)) {
        std::cout << "BatchBlock::getBatchBlockSeqNr: " << __LINE__ << std::endl;
        return -1; // FIXME is this the correct return value on error ?
    }
    std::cout << "BatchBlock::getBatchBlockSeqNr: " << __LINE__ << " sequence: " << batch->sequence << std::endl;
    uint64_t rtvl = batch->sequence;
    //delete batch;
    return rtvl;
    return batch->sequence;
    return rand() % 31;
}
