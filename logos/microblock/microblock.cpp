//===-- logos/microblock/microblock.cpp - Microblock and MicroBlockHandler class definition -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of the MicroBlock and MicroBlockHandler classes, which are used
/// in the Microblock processing
///
//===----------------------------------------------------------------------===//
#include <logos/microblock/microblock.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/merkle.hpp>
#include <logos/node/common.hpp>
#include <logos/node/node.hpp>
using namespace logos;

const size_t MicroBlock::HASHABLE_BYTES = sizeof(MicroBlock)
                                            - sizeof(Signature);

// batch block hash and timestamp
struct entry {
    BlockHash hash;
    uint64_t timestamp;
};

bool
MicroBlockHandler::BuildMicroBlock(
    MicroBlock &block) //!< block to build in/out
{
    vector<BlockHash> merkle; // holds first level of parents
    BlockHash previous_hash(0); // previous leaf's hash in merkle tree
    BlockHash hash;
    MicroBlock previous; 
    uint64_t interval_msec = interval.count() * 1000; // interval is in seconds, timestamp msec

    // is this the very first microblock (TBD have to point to the current epoch if first)
    bool no_previous = store.micro_block_tip_get(hash);
    no_previous = !no_previous ? store.micro_block_get(hash, previous) : no_previous; 

    // delegate's chain from the current tip to the previous microblock tips or complete chain if 
    // no previous
    std::vector<std::vector<entry>> blocks(nDelegates);

    // get the most recent time stamp from the batch block tips
    // need it to include the batch blocks within the time interval
    for (uint8_t delegate = 0; delegate < nDelegates; ++delegate) 
    {
        // get the delegate's chain tip
        if (false == store.batch_tip_get(delegate, hash)) // ignore if can't get it?
        {
            BatchStateBlock batch;
            bool not_found = false; // is it possible we can't find the block?
            // iterate over blocks in the delegate's chain
            // done with this delegate's blockchain if reached the previous tip
            // might get the whole chain if no previous (first microblock ever; what about first in epoch)
            // what if delegates are different?
            for (not_found = store.batch_block_get(hash, batch); !not_found && hash != previous.tips[delegate]; 
                hash = batch.hash, not_found = store.batch_block_get(hash, batch)) 
            {
                // collect the blocks for this delegate
                blocks[delegate].push_back(entry{hash,batch.timestamp});
            }
        }
    }

    assert(blocks.size() != 0);

    // get the oldest timestamp, the blocks has batch-blocks with the most recent first
    uint64_t base_timestamp = blocks[0].back().timestamp;
    for (uint8_t delegate = 1; delegate < nDelegates; ++delegate) 
    {
        if (blocks[delegate].back().timestamp < base_timestamp)
            base_timestamp = blocks[delegate].back().timestamp;
    }

    // prepare merkle tree nodes, pre-calculate first level of parents as we are iteratig over the delegate's blockchains
    for (uint8_t delegate = 0; delegate < nDelegates; ++delegate) 
    {
        // iterate oldest first
        for (auto it = blocks[delegate].rbegin(); it != blocks[delegate].rend(); ++it)
        {
           if (it->timestamp - base_timestamp < interval_msec ) 
           {
               ++block.numberBlocks;
               block.tips[delegate] = it->hash;
               if (block.numberBlocks % 2 == 0)
                    merkle.push_back(Hash(previous_hash, it->hash));
               else
                    previous_hash = it->hash;
           } 
           else
               break;
        }
    }

    // odd number of blocks, duplicate the last block
    if (block.numberBlocks % 2)
        merkle.push_back(Hash(previous_hash, previous_hash));
    
    // is it possible we don't have any blocks?
    assert (block.numberBlocks != 0);

    block.previous = previous.previous; // TBD, previous microblock's hash but could be epoch
    block.merkleRoot = MerkleRoot(merkle); 
    block.timestamp = logos::seconds_since_epoch(); // TBD, either consensus stamp or the most recent stamp of included batch block
    block.delegateNumber = 0; // TBD, delegate who proposed this block (in this case self)
    block.epochNumber = 0; // TBD, current epoch
    block.microBlockNumber = 0; // TBD, microblock number in this epoch
    block.signature = {0}; // TBD, the consensus's multisig

    return true;
}

//!< Start periodic microblock processing
void MicroBlockHandler::Start(
    std::function<void(MicroBlock &)> cb) //!< call back to process generated microblock
{
    alarm.add(std::chrono::steady_clock::now () + interval,[&]()mutable->void{
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
