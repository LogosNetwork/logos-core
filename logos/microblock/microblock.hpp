//===-- logos/microblock/microblock.hpp - Microblock and MicroBlockHandler class declaration -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// @file
/// This file contains the declaration of the MicroBlock and MicroBlockHandler classes, which are used
/// in the Microblock processing
///
//===----------------------------------------------------------------------===//
#pragma once
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/merkle.hpp>
#include <logos/lib/numbers.hpp>

// TBD MUST HAVE .h file with global system constants or access to global configuration
// Perhaps node_config should be singleton?
const uint8_t NUMBER_DELEGATES = 32;

namespace logos {
    class block_store;
}
using BlockHash = logos::block_hash;
using BlockStore = logos::block_store;

/// Microblocks are created periodically. Primary delegate is the first one to propose the microblock. 
/// If primary delegate fails to propose then next in line delegate (based on the voting power) proposes
/// the microblock. Microblock is used for checkpointing and bootstraping.
struct MicroBlock : MessageHeader<MessageType::Pre_Prepare, ConsensusType::MicroBlock> {
    MicroBlock() : MessageHeader(0), previous(0), merkleRoot(0), numberBlocks(0), delegateNumber(0),
        epochNumber(0), microBlockNumber(0) { tips={0};signature={0};}
    /// Calculate block's hash
    BlockHash Hash() const {
        return ::Hash([&](function<void(const void *data,size_t)> cb)mutable->void {
            cb(previous.bytes.data(), sizeof(previous));
            cb(merkleRoot.bytes.data(), sizeof(previous));
            cb(&timestamp, sizeof(timestamp));
            cb(&numberBlocks, sizeof(numberBlocks));
            cb(&delegateNumber, sizeof(delegateNumber));
            cb(&epochNumber, sizeof(epochNumber));
            cb(&microBlockNumber, sizeof(microBlockNumber));
            cb(signature.data(), sizeof(signature));
        });
    }
    /// Overide to mirror state_block
    BlockHash hash() const { return Hash(); }
    static const size_t HASHABLE_BYTES; //<! hashable bytes of the micrblock - used in signing
    const uint8_t version = 0; ///< Core's software version
    BlockHash previous; ///< Previous microblock'hash or current epoch if this is the first block
    BlockHash merkleRoot; ///< Merkle root of the batch blocks included in this microblock
    //uint64_t timestamp; ///< Consensus time stamp
    uint16_t numberBlocks; ///< Number of batch bloks in this microblock
    uint8_t delegateNumber; ///< Delegate number (0-31) who proposed this microblock
    uint epochNumber; ///< Current epoch
    uint8_t microBlockNumber; ///< Microblock number within this epoch
    std::array<BlockHash,NUMBER_DELEGATES> tips; ///< Delegate's batch block tips
    std::array<uint8_t, 32> signature; ///< Multisignature
};

namespace logos {
class alarm;
}
/// Handle MicroBlock processing
class MicroBlockHandler : public std::enable_shared_from_this<MicroBlockHandler> {
    logos::alarm &alarm;
    BlockStore &store; ///< reference to the block store
    uint8_t nDelegates; ///< number of delegates
    std::chrono::seconds interval; ///< microblock generation interval (seconds)
public:
    /// Class constructor
    /// @param s logos::alarm reference
    /// @param s logos::block_store reference
    /// @param n number of delegates
    /// @param i microblock process period interval
    MicroBlockHandler(logos::alarm &a, BlockStore &s, uint8_t n, std::chrono::seconds i) : alarm(a), store(s), nDelegates(n),
        interval(i) {}

    /// Class distructor
    virtual ~MicroBlockHandler() {}

    /// Start periodic microblock processing
    /// called by node::start
    /// @param callback function that takes as the argument generated microblock
    void Start(
        std::function<void(MicroBlock&)>
    );

    /// Build the block, called periodically by node
    /// Could be called by any delegate
    /// @param block to build [in|out]
    /// @returns true on success
    bool BuildMicroBlock(MicroBlock &block);

    /// Verify the proposed block
    /// @param block to verify [in]
    /// @return true if verified (TBD, perhaps enum to address all possible failure scenarios
    /// 1. exists and matches; 2. doesn't exist but all data matches 3. doesn't exist and there is
    /// a different block matching the same parent. 4. doesn't exist and there is no parent that this
    /// block references, the block # is ahead of the current block #.)
    bool VerifyMicroBlock(MicroBlock &block); ///< Verify this microblock either exists or can be built and matches this block
};