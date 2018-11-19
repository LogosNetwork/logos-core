#pragma once

#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock.hpp>

namespace Micro
{
    using Store   = ConsensusContainer::Store;

    BlockHash getMicroBlockTip(Store& s, int delegate);
    uint64_t  getMicroBlockSeqNr(Store& s, int delegate);
    BlockHash getNextMicroBlock (Store &s, int delegate, MicroBlock &b);
    BlockHash getNextMicroBlock (Store &s, int delegate, BlockHash &h );
    std::shared_ptr<MicroBlock> readMicroBlock(Store &s, BlockHash &h );
    void dumpMicroBlockTips(Store &store, BlockHash &hash);

}/* namespace Micro */
