#pragma once

#include <logos/consensus/consensus_container.hpp>
#include <logos/epoch/epoch.hpp>

namespace EpochBlock {

using Store = ConsensusContainer::Store;

BlockHash getEpochBlockTip(Store& s);
uint64_t  getEpochBlockSeqNr(Store& s);
BlockHash getNextEpochBlock(Store &store, BlockHash &h);
BlockHash getPrevEpochBlock(Store &store, BlockHash &h);
std::shared_ptr<Epoch> readEpochBlock(Store &store, BlockHash &h);

}
