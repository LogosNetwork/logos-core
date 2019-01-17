#pragma once

#include <logos/consensus/consensus_container.hpp>
#include <logos/epoch/epoch.hpp>

namespace EpochBlock {

using Store = ConsensusContainer::Store;

/// getEpochBlockTip
/// @param store BlockStore
/// @returns BlockHash of the epoch block tip
BlockHash getEpochBlockTip(Store& s);

/// getEpochBlockSeqNr
/// @param store BlockStore
/// @returns uint64_t representing a sequence number
uint64_t  getEpochBlockSeqNr(Store& s);

/// getNextEpochBlock
/// @param store BlockStore
/// @param h BlockHash
/// @returns BlockHash representing the next epoch block in the chain
BlockHash getNextEpochBlock(Store &store, BlockHash &h);

/// getPrevEpochBlock
/// @param store BlockStore
/// @param h BlockHash
/// @returns BlockHash representing the previous block in the chain
BlockHash getPrevEpochBlock(Store &store, BlockHash &h);

/// readEpochBlock
/// @param store BlockStore
/// @param h BlockHash
/// @returns shared pointer of Epoch block.
std::shared_ptr<Epoch> readEpochBlock(Store &store, BlockHash &h);

}
