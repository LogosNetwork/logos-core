/// @file
/// This file contains the declaration of the MicroBlockHandler classe, which is used
/// in the Microblock processing
#pragma once
#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/lib/epoch_time_util.hpp>

namespace logos {
    class block_store;
}
class IRecallHandler;

using BlockStore                    = logos::block_store;
using IteratorBatchBlockReceiverCb  = std::function<void(uint8_t, const BatchStateBlock&)>;
using BatchBlockReceiverCb          = std::function<void(const BatchStateBlock&)>;

/// Handle MicroBlock processing
class MicroBlockHandler : public PersistenceManager<MBCT> {

    using BatchTips = BlockHash[NUM_DELEGATES];

public:
    /// Class constructor
    /// @param s logos::alarm reference
    /// @param s logos::block_store reference
    /// @param n number of delegates
    /// @param i microblock process period interval
    MicroBlockHandler(BlockStore &s,
                      IRecallHandler & recall_handler)
        : PersistenceManager<MBCT>(s)
        , _recall_handler(recall_handler)
        {}

    /// Class distructor
    virtual ~MicroBlockHandler() {}

    /// Build the block, called periodically by node
    /// Could be called by any delegate
    /// @param block to build [in|out]
    /// @param last_micro_block last microblock in the poch [in]
    /// @returns true on success
    bool Build(MicroBlock &block, bool last_micro_block);

    /// Verify this microblock either exists or can be built and matches this block
    /// @param block to validate [in]
    /// @return true if validated
    //bool Validate(const MicroBlock &block);

    /// Verify this microblock either exists or can be built and matches this block
    /// @param block to save to the database [in]
    //void ApplyUpdates(const MicroBlock &block);

    /// Verify this microblock either exists or can be built and matches this block
    /// @param block to save to the database [in]
    /// @param transaction transaction [in]
    //void ApplyUpdates(const MicroBlock &block, const logos::transaction &);

private:

    /// Iterates each delegates' batch state block chain.
    /// @param start tips to start iteration [in]
    /// @param end tips to end iteration [in]
    /// @param cb function to call for each delegate's batch state block, the function's argument are
    ///   delegate id and BatchStateBlock
    //void BatchBlocksIterator(const BatchTips &start, const BatchTips &end, IteratorBatchBlockReceiverCb cb);

    /// Calculate Merkle root and get batch block tips.
    /// If the previous micro block' time stamp (PMBTS) is not 0 (genesis block time stamp is 0)
    /// then iterate over delegates batch block chain and select blocks with the time stamp
    /// less than the PMBTS + micro block cut-off time (10 minutes). Calculate Merkle Tree root
    /// from the selected blocks.
    /// @param start tips to start iteration [in]
    /// @param end tips to end iteration [in]
    /// @param tips new batch block tips [in|out]
    /// @param num_blocks number of selected batch blocks [out]
    /// @param timestamp timestamp of the previous microblock [in]
    /// @returns Merkle root
    BlockHash FastMerkleTree(const BatchTips &start, const BatchTips &end, BatchTips &tips, uint &num_blocks,
            const uint64_t timestamp);

    /// Calculate Merkle root and get batch block tips.
    /// Genesis micro block time stamp is 0. Therefore, the first micro block following the genesis
    /// micro block has to collect all batch state blocks from the current batch state block tips
    /// and find the oldest time stamp (OTS) of these blocks. Then the algorithm is as above
    /// with OTS replacing PMBTS.
    /// @param start tips to start iteration [in]
    /// @param end tips to end iteration [in]
    /// @param tips new batch block tips [in|out]
    /// @param num_blocks number of selected batch blocks [out]
    /// @returns Merkle root
    BlockHash SlowMerkleTree(const BatchTips &start, const BatchTips &end, BatchTips &tips, uint &num_blocks);

    /// Get tips to include in the micro block
    /// @param start tips to start iteration [in]
    /// @param end tips to end iteration [in]
    /// @param tips new batch block tips [in|out]
    /// @param num_blocks number of selected batch blocks [out]
    /// @param timestamp timestamp of the previous microblock [in]
    void GetTipsFast(const BatchTips &start, const BatchTips &end, BatchTips &tips, uint &num_blocks,
            const uint64_t timestamp);

    /// Get tips to include in the micro block
    /// @param start tips to start iteration [in]
    /// @param end tips to end iteration [in]
    /// @param tips new batch block tips [in|out]
    /// @param num_blocks number of selected batch blocks [out]
    void GetTipsSlow(const BatchTips &start, const BatchTips &end, BatchTips &tips, uint &num_blocks);

    /// Get microblock cut-off time in milliseconds
    /// @param timestamp the base time stamp
    /// @returns cut-off time
    uint64_t GetCutOffTimeMsec(const uint64_t timestamp)
    {
        return (timestamp + TConvert<Milliseconds>(MICROBLOCK_CUTOFF_TIME).count());
    }

    //BlockStore &            _store; 		    ///< reference to the block store
    IRecallHandler &        _recall_handler;    ///< recall handler reference
    //Log                     _log;               ///< boost asio log
};
