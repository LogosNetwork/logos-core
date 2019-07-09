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

using BlockStore                    = logos::block_store;
using BatchBlockReceiverCb          = std::function<void(const ApprovedRB &)>;

/// MicroBlockHandler builds MicroBlock
class MicroBlockHandler
{

public:
    /// Class constructor
    /// @param s logos::alarm reference
    /// @param s logos::block_store reference
    /// @param n number of delegates
    /// @param i microblock process period interval
    MicroBlockHandler(BlockStore &store,
                      IRecallHandler & recall_handler)
            : _store(store)
            , _recall_handler(recall_handler)
    {}

    /// Class destructor
    virtual ~MicroBlockHandler() {}

    /// Build the block, called periodically by node
    /// Could be called by any delegate
    /// @param block to build [in|out]
    /// @returns true on success
    bool Build(MicroBlock &block);

    /// Get microblock cut-off time in milliseconds
    /// @param timestamp the base time stamp
    /// @param add_cutoff if true then add cut off
    /// @returns cut-off time
    static uint64_t GetCutOffTimeMsec(const uint64_t timestamp, bool add_cutoff = false)
    {
        return (timestamp + ((add_cutoff)?TConvert<Milliseconds>(MICROBLOCK_CUTOFF_TIME).count():0));
    }

private:

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
    BlockHash FastMerkleTree(const BatchTipHashes &start, const BatchTipHashes &end, BatchTipHashes &tips, uint &num_blocks,
                             const uint64_t timestamp);

    /// Calculate Merkle root and get batch block tips.
    /// Genesis micro block time stamp is 0. Therefore, the first micro block following the genesis
    /// micro block has to collect all batch state blocks from the current batch state block tips
    /// and find the oldest time stamp (OTS) of these blocks. Then the algorithm is as above
    /// with OTS replacing PMBTS.
    /// @param start tips to start iteration, current BSB tips [in]
    /// @param end previous microblock tips [in]
    /// @param tips new batch block tips [in|out]
    /// @param num_blocks number of selected batch blocks [out]
    /// @returns Merkle root
    BlockHash SlowMerkleTree(const BatchTipHashes &start, const BatchTipHashes &end, BatchTipHashes &tips, uint &num_blocks);

    /// Get tips to include in the micro block.
    /// Walk from the previous microblock tips to the cutoff time.
    /// @param start previous microblock tips [in]
    /// @param tips new batch block tips [in|out]
    /// @param num_blocks number of selected batch blocks [out]
    /// @param cutoff timestamp of the previous microblock, used as cutoff time [in]
    void GetTipsFast(const BatchTips &start, const uint64_t cutoff, BatchTips &tips, uint &num_blocks);

    /// Get tips to include in the first micro block after the genesis microblock.
    /// The previous microblock doesn't have the timestamp, so have to find the very
    /// first BSB for each delegate and then select the min timestamp. Then add 10 min and
    /// remainder from min timestamp to the nearest 10 min. Then have to walk the BSB tips again to
    /// select BSB with the timestamp less than the cut-off time.
    /// @param start tips to start iteration, current BSB tips [in]
    /// @param end previous microblock tips [in]
    /// @param tips new batch block tips [in|out]
    /// @param num_blocks number of selected batch blocks [out]
    void GetTipsSlow(const BatchTipHashes &start, const BatchTipHashes &end, BatchTips &tips, uint &num_blocks);

    BlockStore &            _store;
    IRecallHandler &        _recall_handler;    ///< recall handler reference
    Log                     _log;
};