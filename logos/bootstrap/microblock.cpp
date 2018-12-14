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

uint64_t  Micro::getMicroBlockSeqNr(Store& s) // TODOFUNC
{
    BlockHash hash = Micro::getMicroBlockTip(s);
    std::shared_ptr<MicroBlock> tip = Micro::readMicroBlock(s,hash);
    return tip->sequence;
}

BlockHash Micro::getNextMicroBlock(Store &store, MicroBlock &b) // TODOFUNC
{
    BlockHash h;
    return h;
}

BlockHash Micro::getNextMicroBlock(Store &store, BlockHash &hash) // TODOFUNC
{
    MicroBlock micro;
    if(hash.is_zero()) {
        return hash;
    }
    store.micro_block_get(hash, micro);
    return micro.next;
}

BlockHash Micro::getPrevMicroBlock(Store &store, BlockHash &hash) // TODOFUNC
{
    MicroBlock micro;
    if(hash.is_zero()) {
        return hash;
    }
    store.micro_block_get(hash, micro);
    return micro.previous;
}

std::shared_ptr<MicroBlock> Micro::readMicroBlock(Store &store, BlockHash &hash) // TODOFUNC
{
    std::shared_ptr<MicroBlock> micro(new MicroBlock);
    if(!store.micro_block_get(hash,*micro)) {
        return micro;
    }
    return nullptr;
}

void Micro::dumpMicroBlockTips(Store &store, BlockHash &hash)
{
#ifdef _DEBUG
    std::shared_ptr<MicroBlock> micro = Micro::readMicroBlock(store,hash);
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        std::cout << "Micro::dumpMicroBlockTips: " << micro->tips[i].to_string() << std::endl;
    }
#endif
}
