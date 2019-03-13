#pragma once

#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/messages/batch_state_block.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/epoch/epoch.hpp>

namespace Bootstrap
{
	using Store      = logos::block_store;

    /// getBatchBlockTip
    /// @param s Store reference
    /// @param delegate int
    /// @returns BlockHash (the tip we asked for)
	BlockHash getBatchBlockTip(Store &s, int delegate);

    /// getBatchBlockSeqNr
    /// @param s Store reference
    /// @param delegate int
    /// @returns uint64_t (the sequence number)
	uint64_t  getBatchBlockSeqNr(Store &s, int delegate);


    /// getMicroBlockTip
    /// @param s BlockStore
    /// @returns BlockHash representing the Micro block tip
    BlockHash getMicroBlockTip(Store& s);

    /// getMicroBlockSeqNr
    /// @param s BlockStore
    /// @returns uint64_t representing the sequence number
    uint64_t  getMicroBlockSeqNr(Store& s);

    /// getMicroBlockSeqNr
    /// @param s BlockStore
    //  @param hash the block to get sequence number from
    /// @returns uint64_t representing the sequence number
    uint64_t  getMicroBlockSeqNr(Store& s, BlockHash& hash);

    /// getNextMicroBlock
    /// @param s BlockStore
    /// @param h BlockHash
    /// @returns BlockHash of the next micro block given hash of the current
    BlockHash getNextMicroBlock (Store &s, BlockHash &h );

    /// getPrevMicroBlock
    /// @param s BlockStore
    /// @param h BlockHash
    /// @returns BlockHash of the previous micro block given hash of the current
    BlockHash getPrevMicroBlock (Store &s, BlockHash &h );

    /// readMicroBlock read micro block from database
    /// @param s BlockStore
    /// @param h BlockHash
    /// @returns shared pointer of micro block
    std::shared_ptr<ApprovedMB> readMicroBlock(Store &s, BlockHash &h );

    /// dumpMicroBlockTips print bsb block tips to std::cout for debug
    /// @param store
    /// @param hash representing micro block
    void dumpMicroBlockTips(Store &store, BlockHash &hash);

	/// getEpochBlockTip
	/// @param store BlockStore
	/// @returns BlockHash of the epoch block tip
    BlockHash getEpochBlockTip(Store& s);

/// getEpochBlockSeqNr
/// @param store BlockStore
/// @returns uint64_t representing a sequence number
    uint64_t  getEpochBlockSeqNr(Store& s);

/// getEpochBlockSeqNr
/// @param store BlockStore
/// @returns uint64_t representing a sequence number
    uint64_t  getEpochBlockSeqNr(Store& s, BlockHash& hash);

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
    std::shared_ptr<ApprovedEB> readEpochBlock(Store &store, BlockHash &h);

}
