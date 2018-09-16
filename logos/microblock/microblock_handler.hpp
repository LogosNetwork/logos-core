/// @file
/// This file contains the declaration of the MicroBlockHandler classe, which is used
/// in the Microblock processing
#pragma once
#include <logos/consensus/message_validator.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/lib/epoch_time_util.hpp>

namespace logos {
    class block_store;
}
class IRecallHandler;

using BlockStore = logos::block_store;

/// Handle MicroBlock processing
class MicroBlockHandler {

    using Log       = boost::log::sources::logger_mt;
    using BatchTips = std::array<BlockHash,NUM_DELEGATES>;

public:
    /// Class constructor
    /// @param s logos::alarm reference
    /// @param s logos::block_store reference
    /// @param n number of delegates
    /// @param i microblock process period interval
    MicroBlockHandler(BlockStore &s,
                      uint8_t delegate_id,
                      IRecallHandler & recall_handler)
        : _store(s)
        , _delegate_id(delegate_id)
        , _recall_handler(recall_handler)
        {}

    /// Class distructor
    virtual ~MicroBlockHandler() {}

    /// Start periodic microblock processing
    /// called by node::start
    /// @param callback function that takes as the argument generated microblock
    void Start(
        std::function<void(std::shared_ptr<MicroBlock>)>
    );

    /// Build the block, called periodically by node
    /// Could be called by any delegate
    /// @param block to build [in|out]
    /// @param last_micro_block last microblock in the poch [in]
    /// @returns true on success
    bool BuildMicroBlock(MicroBlock &block, bool last_micro_block);

    /// Verify this microblock either exists or can be built and matches this block
    /// @param block to verify [in]
    /// @return true if verified (TBD, perhaps enum to address all possible failure scenarios
    /// 1. exists and matches; 2. doesn't exist but all data matches 3. doesn't exist and there is
    /// a different block matching the same parent. 4. doesn't exist and there is no parent that this
    /// block references, the block # is ahead of the current block #.)
    bool VerifyMicroBlock(const MicroBlock &block);

    /// Verify this microblock either exists or can be built and matches this block
    /// @param block to save to the database [in]
    void ApplyUpdates(const MicroBlock &block);

    /// Verify this microblock either exists or can be built and matches this block
    /// @param block to save to the database [in]
    /// @param transaction transaction [in]
    /// @returns microblock hash
    BlockHash ApplyUpdates(const MicroBlock &block, const logos::transaction &);

private:

    /// Walk delegates' batch block chain
    /// @param start tips to start the walk [in]
    /// @param end tips to end the walk [in]
    /// @param cb function to call for each delegate's batch block
    void WalkBatchBlocks(const BatchTips &start, const BatchTips &end,
            std::function<void(uint8_t, const BatchStateBlock&)>);

    /// Walk delegates' batch block chain
    /// @param start tips to start the walk [in]
    /// @param end tips to end the walk [in]
    /// @param tips foound batch block tips [in]
    /// @param num_blocks number of selected batch blocks [in]
    /// @param timestamp timestamp of the previous microblock [in]
    /// @returns Merkle root
    BlockHash FastMerkleTree(const BatchTips &start, const BatchTips &end, BatchTips &tips, uint &num_blocks,
            uint64_t timestamp);

    /// Calculate Merkle root and get batch block tips
    /// @param start tips to start the walk [in]
    /// @param end tips to end the walk [in]
    /// @param tips foound batch block tips [in]
    /// @param num_blocks number of selected batch blocks [in]
    /// @returns Merkle root
    BlockHash SlowMerkleTree(const BatchTips &start, const BatchTips &end, BatchTips &tips, uint &num_blocks);

    BlockStore &            _store; 		    ///< reference to the block store
    uint8_t                 _delegate_id;       ///< local delegate id
    IRecallHandler &        _recall_handler;    ///< recall handler reference
    Log                     _log;               ///< boost asio log
};
