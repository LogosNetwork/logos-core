/// @file
/// This file contains the definition of the MicroBlockHandler class, which is used
/// in the Microblock processing
#include <logos/microblock/microblock_handler.hpp>
#include <logos/blockstore.hpp>
#include <logos/node/node.hpp>
#include <time.h>
using namespace logos;

/// Microblock cut off time is calculated as Tc = TEi + Mi * 10 where TEi is the i-th epoch (previous epoch),
/// Mi is current microblock sequence number
bool
MicroBlockHandler::BuildMicroBlock(
    MicroBlock &block)   ///< block to build in/out
{
    vector<BlockHash> merkle; // holds first level of parents
    BlockHash previous_hash(0); // previous leaf's hash in merkle tree
    BlockHash previous_micro_block_hash;
    BlockHash hash;
    MicroBlock previous; // previous microblock
    uint64_t interval_cutoff_msec = _interval_cutoff.count() * 1000; // interval is in seconds, timestamp msec

    // is this the very first microblock then previous points to the genesis microblock
    // with epoch number -1
    assert(false == _store.micro_block_tip_get(previous_micro_block_hash));
    assert(false == _store.micro_block_get(previous_micro_block_hash, previous));

    // the first microblock
    bool first_ever_micro_block = previous._epoch_number == -1;

    // batch block is inserted into the microblock if the batch block's time stamp is
    // greater than the time stamp of the previous tip batch block time stamp and less or equal to the
    // tip batch block time stamp plus cut-off time.
    // we can start building the merkle tree while iterating over batch block for each delegate
    // because the batch blocks are ordered within each delegate
    block._number_batch_blocks = 0;
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        auto get_tip_time = [this, delegate, first_ever_micro_block, &previous]() -> uint64_t {
            BatchStateBlock block;
            if (false == first_ever_micro_block && false == _store.batch_block_get(previous._tips[delegate], block))
            {
               return block.timestamp;
            }
           return previous.timestamp;
        };

        uint64_t batch_block_tip_time = get_tip_time();
        // get the delegate's chain tip
        // could be empty, for instance the delegate is off line for a while?
        if (false == _store.batch_tip_get(delegate, hash))
        {
            BatchStateBlock batch;
            bool not_found = false; // is it possible we can't find the block?
            // iterate over batch blocks in the delegate's chain
            // done with this delegate's blockchain if reached the previous tip (to the end for the genesis)
            // might get the whole chain if no previous (first microblock ever; what about first in epoch)
            // what if delegates are different?
            for (not_found = _store.batch_block_get(hash, batch);
                    !not_found && hash != previous._tips[delegate]; // tips are 0 in genesis
                    hash = batch.previous, not_found = _store.batch_block_get(hash, batch))
            {
                if ((first_ever_micro_block || batch.timestamp > batch_block_tip_time) &&
                    batch.timestamp <= (batch_block_tip_time + interval_cutoff_msec))
                {
                    ++block._number_batch_blocks;
                    // keep the batch block's tip for this delegate
                    if (block._tips[delegate] == 0)
                    {
                       block._tips[delegate] = hash;
                    }

                    if (block._number_batch_blocks %2 == 0)
                    {
                        merkle.push_back(Hash(previous_hash, hash));
                    }
                    else
                    {
                        previous_hash = hash;
                    }
                }
            }
        }
    }

    // odd number of blocks, duplicate the last block
    if (block._number_batch_blocks % 2)
        merkle.push_back(Hash(previous_hash, previous_hash));

    assert (block._number_batch_blocks != 0);

    // should it be allowed to have no tips?
    assert(std::count(block._tips.begin(), block._tips.end(), 0) == 0);

    Epoch epoch;
    assert(false == _store.epoch_tip_get(hash));
    assert(false == _store.epoch_get(hash, epoch));
    // first micro block in this epoch
    bool first_micro_block = epoch._micro_block_tip == previous_micro_block_hash;

    block._epoch_number = first_micro_block ? previous._epoch_number + 1 : previous._epoch_number;
    block.previous = previous_micro_block_hash;
    block._merkle_root = MerkleRoot(merkle);
    block.timestamp = GetStamp();
    block._delegate = _delegate_id;
    block._micro_block_number = first_micro_block ? 0 : previous._micro_block_number + 1;

    return true;
}

//!< Start periodic microblock processing
void MicroBlockHandler::Start(
    std::function<void(std::shared_ptr<MicroBlock>)> cb) ///< call back to process generated microblock
{
    time_t rawtime;
    struct tm *ptm;

    time(&rawtime);
    ptm = gmtime(&rawtime);
    ptm->tm_min; // 0-59
    // have to run on every 0,20,40 min on the hour
    int n = ptm->tm_min / MICROBLOCK_PROPOSAL_TIME;
    int i = (n + 1) * MICROBLOCK_PROPOSAL_TIME - ptm->tm_min;
    _alarm.add(std::chrono::steady_clock::now () + std::chrono::minutes(i), [&]()mutable->void{
        auto block(std::make_shared<MicroBlock>());
        BuildMicroBlock(*block);
        cb(block);
	});
}

bool MicroBlockHandler::VerifyMicroBlock(MicroBlock &block)
{
    return true;
}
