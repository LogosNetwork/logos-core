#pragma once

#include <logos/consensus/consensus_container.hpp>
#include <logos/epoch/epoch.hpp>

namespace EpochBlock {

using Store = ConsensusContainer::Store;

BlockHash getEpochBlockTip(Store& s, int delegate);
uint64_t  getEpochBlockSeqNr(Store& s, int delegate);
BlockHash getNextEpochBlock(Store &store, int delegate, BlockHash &h);
BlockHash getPrevEpochBlock(Store &store, int delegate, BlockHash &h);
std::shared_ptr<Epoch> readEpochBlock(Store &store, BlockHash &h);

}
