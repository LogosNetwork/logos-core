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
    BatchStateBlock batch;
    BlockHash hash = BatchBlock::getBatchBlockTip(store,delegate);
    if(hash.is_zero()) {
        return 0; // FIXME is this the correct return value on error ?
    }
    store.batch_block_get(hash, batch);
    return batch.sequence;
}
