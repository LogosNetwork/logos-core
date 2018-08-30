/// @file
/// This file contains the declaration of the MicroBlock and MicroBlockHandler classes, which are used
/// in the Microblock processing
#pragma once
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/merkle.hpp>
#include <logos/lib/numbers.hpp>

namespace logos {
    class block_store;
}
using BlockHash = logos::block_hash;
using BlockStore = logos::block_store;

/// Microblocks are used for checkpointing and boostrapping. Microblock proposal time is every 20 minutes. 
/// Microblock cut off time is 10 minutes. Microblock previous always references the previous microblock. 
/// The last microblock (#720) in current epoch triggers generation of the next epoch.
/// Primary delegate is the first one to propose the microblock. 
/// If primary delegate fails to propose then next in line delegate (based on the voting power) proposes
/// the microblock. 
struct MicroBlock : MessageHeader<MessageType::Pre_Prepare, ConsensusType::MicroBlock> {
    MicroBlock()
        : MessageHeader(0)
        , _previous(0)
        , _merkle_root(0)
        , _delegate(0)
        , _epoch_number(0)
        , _micro_block_number(0)
        {
            _tips={0};
            signature={0};
        }
    /// Calculate block's hash
    BlockHash Hash() const {
        return ::Hash([&](function<void(const void *data,size_t)> cb)mutable->void {
            cb(&timestamp, sizeof(timestamp));
            cb(_previous.bytes.data(), sizeof(_previous));
            cb(_merkle_root.bytes.data(), sizeof(_merkle_root));
            cb(&_delegate, sizeof(_delegate));
            cb(&_epoch_number, sizeof(_epoch_number));
            cb(&_micro_block_number, sizeof(_micro_block_number));
            cb(_tips.data(), NUM_DELEGATES * sizeof(BlockHash));
        });
    }
    /// Overide to mirror state_block
    BlockHash hash() const { return Hash(); }
    static const size_t HASHABLE_BYTES;         ///< hashable bytes of the micrblock - used in signing
    BlockHash           _previous; 		        ///< Previous microblock'hash or current epoch if this is the first block
    BlockHash           _merkle_root; 		    ///< Merkle root of the batch blocks included in this microblock
    logos::account      _delegate; 	            ///< Delegate who proposed this microblock
    uint                _epoch_number; 			///< Current epoch
    uint8_t             _micro_block_number;	///< Microblock number within this epoch
    std::array<BlockHash,NUM_DELEGATES> _tips;  ///< Delegate's batch block tips
    Signature           signature; 		        ///< Multisignature
};

namespace logos {
class alarm;
}
/// Handle MicroBlock processing
class MicroBlockHandler : public std::enable_shared_from_this<MicroBlockHandler> {
    logos::alarm &       _alarm;       ///< alarm
    BlockStore &         _store; 		///< reference to the block store
    uint8_t              _n_delegates; 	///< number of delegates
    std::chrono::seconds _interval; 	///< microblock generation interval (seconds)
public:
    /// Class constructor
    /// @param s logos::alarm reference
    /// @param s logos::block_store reference
    /// @param n number of delegates
    /// @param i microblock process period interval
    MicroBlockHandler(logos::alarm &a,
                      BlockStore &s,
                      uint8_t n,
                      std::chrono::seconds i)
        : _alarm(a)
        , _store(s)
        , _n_delegates(n)
        , _interval(i)
        {}

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

    /// Verify this microblock either exists or can be built and matches this block
    /// @param block to verify [in]
    /// @return true if verified (TBD, perhaps enum to address all possible failure scenarios
    /// 1. exists and matches; 2. doesn't exist but all data matches 3. doesn't exist and there is
    /// a different block matching the same parent. 4. doesn't exist and there is no parent that this
    /// block references, the block # is ahead of the current block #.)
    bool VerifyMicroBlock(MicroBlock &block);
};
