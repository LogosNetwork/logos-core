#pragma once

#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock.hpp>

namespace Micro
{
    using Store   = ConsensusContainer::Store;

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

}/* namespace Micro */
