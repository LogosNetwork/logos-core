/// @file
/// This file contains the definition of the MicroBlockHandler class, which is used
/// in the Microblock processing
#include <logos/microblock/microblock_handler.hpp>
//#include <logos/microblock/microblock_tester.hpp>
#include <logos/blockstore.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>
#include <time.h>

using namespace logos;

void
MicroBlockHandler::BatchBlocksIterator(
    BlockStore & store,
    const BatchTips &start,
    const BatchTips &end,
    IteratorBatchBlockReceiverCb batchblock_receiver)
{
    Log log;
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        BlockHash hash = start[delegate];
        BatchStateBlock batch;
        bool not_found = false;
        for (not_found = store.batch_block_get(hash, batch);
             !not_found && hash != end[delegate];
             hash = batch.previous, not_found = store.batch_block_get(hash, batch)) {
            batchblock_receiver(delegate, batch);
        }
        if (not_found && hash != 0)
        {
            LOG_ERROR(log) << "MicroBlockHander::BatchBlocksIterator failed to get batch state block: "
                            << hash.to_string();
            return;
        }
    }
}

BlockHash
MicroBlockHandler::FastMerkleTree(
    const BatchTips &start,
    const BatchTips &end,
    BatchTips &tips,
    uint &num_blocks,
    uint64_t timestamp)
{
   uint64_t cutoff_msec = GetCutOffTimeMsec(timestamp);
   return merkle::MerkleHelper([&](merkle::HashReceiverCb element_receiver)->void {
       BatchBlocksIterator(_store, start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable -> void {
          if (batch.timestamp < cutoff_msec)
          {
              BlockHash hash = batch.Hash();
              if (tips[delegate] == 0)
              {
                  tips[delegate] = hash;
              }
              num_blocks++;
              element_receiver(hash);
          }
       });
   });
}

BlockHash
MicroBlockHandler::SlowMerkleTree(
    const BatchTips &start,
    const BatchTips &end,
    BatchTips &tips,
    uint &num_blocks)
{
    struct pair {
        uint64_t timestamp;
        BlockHash hash;
    };
    array<vector<pair>, NUM_DELEGATES> entries;
    uint64_t min_timestamp = GetStamp() + TConvert<Milliseconds>(CLOCK_DRIFT).count();

    // frist get hashes and timestamps of all blocks; and min timestamp to use as the base
    BatchBlocksIterator(_store, start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable->void{
       entries[delegate].push_back({batch.timestamp, batch.Hash()});
       if (batch.timestamp < min_timestamp)
       {
           min_timestamp = batch.timestamp;
       }
    });

    // iterate over all blocks, selecting the ones that less then cutoff time
    // and calcuate the merkle root with the MerkleHelper
    uint64_t cutoff_msec = GetCutOffTimeMsec(min_timestamp);

    return merkle::MerkleHelper([&](merkle::HashReceiverCb element_receiver)->void {
        for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate) {
            for (auto it : entries[delegate]) {
                if (it.timestamp >= (min_timestamp + cutoff_msec)) {
                    continue;
                }
                if (tips[delegate] == 0)
                {
                    tips[delegate] = it.hash;
                }
                num_blocks++;
                element_receiver(it.hash);
            }
        }
    });
}

void
MicroBlockHandler::GetTipsFast(
    const BatchTips &start,
    const BatchTips &end,
    BatchTips &tips,
    uint &num_blocks,
    uint64_t timestamp)
{
    uint64_t cutoff_msec = GetCutOffTimeMsec(timestamp);
    BatchBlocksIterator(_store, start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable -> void {
        if (batch.timestamp < cutoff_msec)
        {
            BlockHash hash = batch.Hash();
            if (tips[delegate] == 0)
            {
                tips[delegate] = hash;
            }
            num_blocks++;
        }
    });
    // verify tips
    for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
    {
        if (tips[del] == 0)
        {
            tips[del] = end[del];
        }
    }
}

void
MicroBlockHandler::GetTipsSlow(
    const BatchTips &start,
    const BatchTips &end,
    BatchTips &tips,
    uint &num_blocks)
{
    struct pair {
        uint64_t timestamp;
        BlockHash hash;
    };
    array<vector<pair>, NUM_DELEGATES> entries;
    uint64_t min_timestamp = GetStamp() + TConvert<Milliseconds>(CLOCK_DRIFT).count();

    // frist get hashes and timestamps of all blocks; and min timestamp to use as the base
    BatchBlocksIterator(_store, start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable->void{
        entries[delegate].push_back({batch.timestamp, batch.Hash()});
        if (batch.timestamp < min_timestamp)
        {
            min_timestamp = batch.timestamp;
        }
    });

    // iterate over all blocks, selecting the ones that less then cutoff time
    uint64_t cutoff_msec = GetCutOffTimeMsec(min_timestamp);

    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate) {
        for (auto it : entries[delegate]) {
            if (it.timestamp >= (min_timestamp + cutoff_msec)) {
                continue;
            }
            if (tips[delegate] == 0)
            {
                tips[delegate] = it.hash;
            }
            num_blocks++;
        }
    }
}

/// Microblock cut off time is calculated as Tc = TEi + Mi * 10 where TEi is the i-th epoch (previous epoch),
/// Mi is current microblock sequence number
bool
MicroBlockHandler::Build(
    MicroBlock &block,
    bool last_micro_block)
{
    BlockHash previous_micro_block_hash;
    MicroBlock previous_micro_block;

    if (_store.micro_block_tip_get(previous_micro_block_hash))
    {
        LOG_FATAL(_log) << "MicroBlockHandler::Build failed to get micro block tip";
        trace_and_halt();
    }
    if (_store.micro_block_get(previous_micro_block_hash, previous_micro_block))
    {
        LOG_FATAL(_log) << "MicroBlockHandler::Build failed to get micro block: "
                        << previous_micro_block_hash.to_string();
        trace_and_halt();
    }

    // collect current batch block tips
    BatchTips start = {0};
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        if (_store.batch_tip_get(delegate, start[delegate]))
        {
            start[delegate] = 0;
        }
    }

    // first microblock after genesis
    if (previous_micro_block.epoch_number == GENESIS_EPOCH)
    {
        GetTipsSlow(start, previous_micro_block.tips, block.tips,
                block.number_batch_blocks);
    }
    else
    {
        GetTipsFast(start, previous_micro_block.tips, block.tips,
                block.number_batch_blocks, previous_micro_block.timestamp);
    }

    // should be allowed to have no blocks so at least it doesn't crash
    // for instance the node is disconnected for a while
    // (block._number_batch_blocks != 0);

    // should it be allowed to have no tips? same as above, i.e disconnected for a while
    // (std::count(block._tips.begin(), block._tips.end(), 0) == 0);

    Epoch epoch;
    BlockHash hash;
    if (_store.epoch_tip_get(hash))
    {
        LOG_FATAL(_log) << "MicroBlockHandler::Build failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(hash, epoch))
    {
        LOG_FATAL(_log) << "MicroBlockHandler::Build failed to get epoch: "
                        << hash.to_string();
        trace_and_halt();
    }

    // first micro block in this epoch
    bool first_micro_block = epoch.micro_block_tip == previous_micro_block_hash;

    block.timestamp = GetStamp();
    block.previous = previous_micro_block_hash;
    block.epoch_number = first_micro_block
            ? previous_micro_block.epoch_number + 1
            : previous_micro_block.epoch_number;
    block.account = DelegateIdentityManager::_delegate_account;
    block.sequence = first_micro_block
            ? 0
            : previous_micro_block.sequence + 1;
    block.last_micro_block = last_micro_block;

//    LOG_INFO(_log) << "MicroBlockHandler::Build, built microblock:"
//                   << " hash " << block.Hash().to_string()
//                   << " timestamp " << block.timestamp
//                   << " previous " << block.previous.to_string()
//                   << " epoch_number " << block.epoch_number
//                   << " account " << block.account.to_account()
//                   << " sequence " << block.sequence
//                   << " last_micro_block " << (int)block.last_micro_block;

    return true;
}
