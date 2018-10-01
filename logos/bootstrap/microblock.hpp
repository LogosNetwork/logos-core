#pragma once

#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock.hpp>

namespace Micro
{
    using Store   = ConsensusContainer::Store;

    BlockHash getNextMicroBlock (Store &s, int delegate, MicroBlock &b); // TODO
    BlockHash getNextMicroBlock (Store &s, int delegate, BlockHash &h ); // TODO
    std::shared_ptr<MicroBlock> readMicroBlock(Store &s, BlockHash &h ); // TODO

}/* namespace Micro */
