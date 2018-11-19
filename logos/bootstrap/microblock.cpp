#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/batch_block_bulk_pull.hpp>
#include <logos/bootstrap/microblock.hpp>

BlockHash Micro::getMicroBlockTip(Store& s, int delegate)
{
    BlockHash hash;
    if(!s.micro_block_tip_get(hash)) {
        return hash;
    }
    return BlockHash();
}

uint64_t  Micro::getMicroBlockSeqNr(Store& s, int delegate) // TODOFUNC
{
#ifdef _DEBUG
    return 0;
#else
    BlockHash hash = BatchBlock::getMicroBlockTip(s,delegate);
    std::shared_ptr<MicroBlock> tip = Micro::readMicroBlock(s,hash);
    return tip->_micro_block_number;
#endif
}

BlockHash Micro::getNextMicroBlock(Store &store, int delegate, MicroBlock &b) // TODOFUNC
{
    BlockHash h;
    return h;
}

BlockHash Micro::getNextMicroBlock(Store &store, int delegate, BlockHash &hash) // TODOFUNC
{
    MicroBlock micro;
    if(hash.is_zero()) {
        return hash;
    }
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

void Micro::dumpMicroBlockTips(Store &store, BlockHash &hash)
{
    std::shared_ptr<MicroBlock> micro = Micro::readMicroBlock(store,hash);
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        std::cout << "Micro::dumpMicroBlockTips: " << micro->tips[i].to_string() << std::endl;
    }
}
