#include <logos/bootstrap/batch_block_tips.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/epoch.hpp>

BlockHash BatchBlock::getBatchBlockTip(Store& store, int delegate)
{
    BlockHash hash = 0;
    if(!store.batch_tip_get(delegate,hash)) {
        return hash;
    }
    return hash;
}

uint64_t  BatchBlock::getBatchBlockSeqNr(Store& store, int delegate)
{
    // The below code will dump core in bsb dtor.
    // Also, the sequence number is not correct. Need real world database to test.
    BatchStateBlock batch;
    BlockHash hash = BatchBlock::getBatchBlockTip(store,delegate);
    if(hash.is_zero()) {
        return BatchBlock::NOT_FOUND;
    }
    if(store.batch_block_get(hash, batch)) {
        return BatchBlock::NOT_FOUND;
    }
    return batch.sequence;
}
