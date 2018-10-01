#include "batch_block_bulk_pull.hpp"
#include "microblock.hpp"

BlockHash Micro::getNextMicroBlock(Store &store, int delegate, MicroBlock &b) // TODOFUNC
{
    BlockHash h;
    return h;
}

BlockHash Micro::getNextMicroBlock(Store &store, int delegate, BlockHash &hash) // TODOFUNC
{
    MicroBlock micro;
    store.micro_block_get(hash, micro);
    return micro.previous; // TESTING Previous... may need to pass in the end not the start for testing
}

std::shared_ptr<MicroBlock> Micro::readMicroBlock(Store &store, BlockHash &hash) // TODO
{
    std::shared_ptr<MicroBlock> micro(new MicroBlock);
    if(!store.micro_block_get(hash,*micro)) {
        return micro;
    }
    return nullptr;
}
