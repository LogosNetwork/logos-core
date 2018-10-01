#include <logos/bootstrap/batch_block_frontier.hpp>

BlockHash BatchBlock::getEpochBlockTip(Store& s, int delegate) // TODOFUNC
{
    return BlockHash();
}

BlockHash BatchBlock::getMicroBlockTip(Store& s, int delegate)
{
    BlockHash hash;
    if(!s.micro_block_tip_get(hash)) {
        return hash;
    }
    return BlockHash();
}

BlockHash BatchBlock::getBatchBlockTip(Store& s, int delegate)
{
    BlockHash hash;
    if(!s.batch_tip_get(delegate,hash)) {
        return hash;
    }
    return BlockHash();
}

uint32_t  BatchBlock::getEpochBlockSeqNr(Store& s, int delegate) // TODOFUNC
{
    // TESTING
    // return rand() % 11;
    return 0;
}

uint32_t  BatchBlock::getMicroBlockSeqNr(Store& s, int delegate) // TODOFUNC
{
    // TESTING
    // return rand() % 11;
    return 0;
}

uint32_t  BatchBlock::getBatchBlockSeqNr(Store& s, int delegate) // TODOFUNC
{
    // TESTING
    return rand() % 31;
    return 0;
}
