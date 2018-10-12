#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/batch_block_tips.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/epoch.hpp>

BlockHash BatchBlock::getBatchBlockTip(Store& s, int delegate)
{
    BlockHash hash;
    if(!s.batch_tip_get(delegate,hash)) {
        return hash;
    }
    return BlockHash();
}

uint32_t  BatchBlock::getBatchBlockSeqNr(Store& s, int delegate) // TODOFUNC
{
#ifdef _DEBUG
    // TESTING
    return rand() % 31;
#else
    return 0;
#endif
}
