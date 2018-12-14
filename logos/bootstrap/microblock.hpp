#pragma once

#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock.hpp>

namespace Micro
{
    using Store   = ConsensusContainer::Store;

    BlockHash getMicroBlockTip(Store& s);
    uint64_t  getMicroBlockSeqNr(Store& s);
    BlockHash getNextMicroBlock (Store &s, MicroBlock &b);
    BlockHash getNextMicroBlock (Store &s, BlockHash &h );
    BlockHash getPrevMicroBlock (Store &s, BlockHash &h );
    std::shared_ptr<MicroBlock> readMicroBlock(Store &s, BlockHash &h );
    void dumpMicroBlockTips(Store &store, BlockHash &hash);

}/* namespace Micro */
