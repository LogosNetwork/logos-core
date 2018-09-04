/// @file
/// This file contains the definition of the MicroBlock and MicroBlockHandler classes, which are used
/// in the Microblock processing
#include <logos/microblock/microblock.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/merkle.hpp>
#include <logos/node/common.hpp>
#include <logos/node/node.hpp>
using namespace logos;

const size_t MicroBlock::HASHABLE_BYTES = sizeof(MicroBlock)
                                            - sizeof(Signature);

/// batch block hash and timestamp
struct entry {
    BlockHash hash;
    uint64_t timestamp;
};

/// Microblock cut off time is calculated as Tc = TEi + Mi * 10 where TEi is the i-th epoch (previous epoch),
/// Mi is current microblock sequence number
bool
MicroBlockHandler::BuildMicroBlock(
    MicroBlock &block) ///< block to build in/out
{
    vector<BlockHash> merkle; // holds first level of parents
    BlockHash previous_hash(0); // previous leaf's hash in merkle tree
    BlockHash hash;
    MicroBlock previous; 
    uint64_t interval_msec = _interval_cutoff.count() * 1000; // interval is in seconds, timestamp msec

    // is this the very first microblock (TBD have to point to the current epoch if first)
    bool no_previous = _store.micro_block_tip_get(hash);
    no_previous = !no_previous ? _store.micro_block_get(hash, previous) : no_previous;

    // delegate's chain from the current tip to the previous microblock tips or complete chain if 
    // no previous
    std::vector<std::vector<entry>> blocks(_n_delegates);

    // batch block is inserted into the microblock if the batch block's time stamp is
    // greater than the time stamp of the previous tip batch block time stamp and less or equal to the
    // tip batch block time stamp plus cut-off time.
    // we can start building the merkle tree while iterating over batch block for each delegate
    // because the batch blocks are ordered within each delegate
    block._number_batch_blocks = 0;
    for (uint8_t delegate = 0; delegate < _n_delegates; ++delegate)
    {
        auto get_tip_time = [this, delegate, no_previous, &previous]() -> uint64_t {
            BatchStateBlock block;
            if (false == no_previous && false == _store.batch_block_get(previous._tips[delegate], block))
            {
               return block.timestamp;
            }
           return 0;
        };

        uint64_t batch_block_tip_time = get_tip_time();
        // get the delegate's chain tip
        if (false == _store.batch_tip_get(delegate, hash)) // ignore if can't get it?
        {
            BatchStateBlock batch;
            bool not_found = false; // is it possible we can't find the block?
            // iterate over blocks in the delegate's chain
            // done with this delegate's blockchain if reached the previous tip
            // might get the whole chain if no previous (first microblock ever; what about first in epoch)
            // what if delegates are different?
            for (not_found = _store.batch_block_get(hash, batch); !not_found && hash != previous._tips[delegate];
                hash = batch.hash, not_found = _store.batch_block_get(hash, batch))
            {
                if (batch.timestamp > batch_block_tip_time &&
                    batch.timestamp <= (batch_block_tip_time + MICROBLOCK_CUTOFF_TIME))
                // collect the blocks for this delegate
                blocks[delegate].push_back(entry{hash,batch.timestamp});
            }
        }
    }

    assert(blocks.size() != 0);

    // get the oldest timestamp, the blocks has batch-blocks with the most recent first
    uint64_t base_timestamp = blocks[0].back().timestamp;
    for (uint8_t delegate = 1; delegate < _n_delegates; ++delegate)
    {
        if (blocks[delegate].back().timestamp < base_timestamp)
            base_timestamp = blocks[delegate].back().timestamp;
    }

    // prepare merkle tree nodes, pre-calculate first level of parents as we are iteratig over the delegate's blockchains
    for (uint8_t delegate = 0; delegate < _n_delegates; ++delegate)
    {
        // iterate oldest first
        for (auto it = blocks[delegate].rbegin(); it != blocks[delegate].rend(); ++it)
        {
           if (it->timestamp - base_timestamp < interval_msec ) 
           {
               ++block._number_batch_blocks;
               block._tips[delegate] = it->hash;
               if (block._number_batch_blocks % 2 == 0)
                    merkle.push_back(Hash(previous_hash, it->hash));
               else
                    previous_hash = it->hash;
           } 
           else
               break;
        }
    }

    // odd number of blocks, duplicate the last block
    if (block._number_batch_blocks % 2)
        merkle.push_back(Hash(previous_hash, previous_hash));
    
    // is it possible we don't have any blocks?
    assert (block._number_batch_blocks != 0);

    block._previous = previous._previous; // TBD, previous microblock's hash but could be epoch
    block._merkle_root = MerkleRoot(merkle);
    block.timestamp = logos::seconds_since_epoch(); // TBD, either consensus stamp or the most recent stamp of included batch block
    block._delegate = 0; // TBD, delegate who proposed this block (in this case self)
    block._epoch_number = 0; // TBD, current epoch
    block._micro_block_number = 0; // TBD, microblock number in this epoch
    block.signature = {0}; // TBD, the consensus's multisig

    return true;
}

//!< Start periodic microblock processing
void MicroBlockHandler::Start(
    std::function<void(MicroBlock &)> cb) ///< call back to process generated microblock
{
    _alarm.add(std::chrono::steady_clock::now () + _interval_proposal, [&]()mutable->void{
        MicroBlock block;
        BuildMicroBlock(block);
        cb(block);
		return;
	}); // start microblock processing
}

bool MicroBlockHandler::VerifyMicroBlock(MicroBlock &block)
{
    return true;
}
