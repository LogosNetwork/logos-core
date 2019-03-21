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
        _store.BatchBlocksIterator(start, end, [&](uint8_t delegate, const ApprovedRB &batch)mutable -> void {
            if (batch.timestamp < cutoff_msec)
            {
                BlockHash hash = batch.Hash();
                if (tips[delegate].is_zero())
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

    // first get hashes and timestamps of all blocks; and min timestamp to use as the base
    _store.BatchBlocksIterator(start, end, [&](uint8_t delegate, const ApprovedRB &batch)mutable->void{
        entries[delegate].push_back({batch.timestamp, batch.Hash()});
        if (batch.timestamp < min_timestamp)
        {
            min_timestamp = batch.timestamp;
        }
    });

    // iterate over all blocks, selecting the ones that less then cutoff time
    // and calcuate the merkle root with the MerkleHelper
    uint64_t cutoff_msec = GetCutOffTimeMsec(min_timestamp, true);

    return merkle::MerkleHelper([&](merkle::HashReceiverCb element_receiver)->void {
        for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate) {
            for (auto it : entries[delegate]) {
                if (it.timestamp >= (min_timestamp + cutoff_msec)) {
                    continue;
                }
                if (tips[delegate].is_zero())
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
        uint64_t cutoff,
        BatchTips &tips,
        uint &num_blocks)
{
    // get 'next' references
    BatchTips next;
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        ApprovedRB batch;
        if (_store.request_block_get(start[delegate], batch))
        {
            next[delegate].clear();
        }
        else
        {
            next[delegate] = batch.next;
        }
    }

    uint64_t cutoff_msec = GetCutOffTimeMsec(cutoff);
    _store.BatchBlocksIterator(next, cutoff_msec, [&](uint8_t delegate, const ApprovedRB &batch)mutable -> void {
        BlockHash hash = batch.Hash();
        tips[delegate] = hash;
        num_blocks++;
    });

    // Verify tips. We might not have any BSB in this microblock.
    // In this case keep previous microblock tips.
    for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
    {
        if (tips[del] == 0)
        {
            tips[del] = start[del];
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

    // first get hashes and timestamps of all blocks; and min timestamp to use as the base
    _store.BatchBlocksIterator(start, end, [&](uint8_t delegate, const ApprovedRB &batch)mutable->void{
        entries[delegate].push_back({batch.timestamp, batch.Hash()});
        if (batch.timestamp < min_timestamp)
        {
            min_timestamp = batch.timestamp;
        }
    });

    // remainder from min_timestamp to the nearest 10min
    auto rem = min_timestamp % TConvert<Milliseconds>(MICROBLOCK_CUTOFF_TIME).count();
    auto cutoff = min_timestamp + ((rem!=0)?TConvert<Milliseconds>(MICROBLOCK_CUTOFF_TIME).count() - rem:0);

    // iterate over all blocks, selecting the ones that are less than cutoff time
    uint64_t cutoff_msec = GetCutOffTimeMsec(cutoff, false);

    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate) {
        for (auto it : entries[delegate]) {
            if (it.timestamp >= cutoff_msec) {
                continue;
            }
            if (tips[delegate].is_zero())
            {
                tips[delegate] = it.hash;
            }
            num_blocks++;
        }
    }
}

bool
MicroBlockHandler::Build(
        MicroBlock &block,
        bool last_micro_block)
{
    BlockHash previous_micro_block_hash;
    ApprovedMB previous_micro_block;

    if (_store.micro_block_tip_get(previous_micro_block_hash))
    {
        LOG_FATAL(_log) << "MicroBlockHandler::Build - failed to get micro block tip";
        trace_and_halt();
    }
    if (_store.micro_block_get(previous_micro_block_hash, previous_micro_block))
    {
        LOG_FATAL(_log) << "MicroBlockHandler::Build - failed to get micro block: "
                        << previous_micro_block_hash.to_string();
        trace_and_halt();
    }
    if (previous_micro_block_hash != previous_micro_block.Hash())
    {
        LOG_FATAL(_log) << "MicroBlockHandler::Build - detected database corruption. "
                        << "Stored micro block has a different hash from its DB key";
        trace_and_halt();
    }

    // collect current batch block tips
    BatchTips start;

    // first microblock after genesis, the cut-off time is the Min timestamp of the very first BSB
    // for all delegates + remainder from Min to nearest 10 min + 10 min; start is the current tips
    if (previous_micro_block.epoch_number == GENESIS_EPOCH)
    {
        for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
        {
            // add 1 because we need the current epoch's tips
            if (_store.request_tip_get(delegate, previous_micro_block.epoch_number + 1, start[delegate]))
            {
                start[delegate].clear();
            }
        }
        GetTipsSlow(start, previous_micro_block.tips, block.tips, block.number_batch_blocks);
    }
    // Microblock cut off time is the previous microblock's proposed time;
    // start points to the first block after the previous, i.e. previous.next
    else
    {
        GetTipsFast(previous_micro_block.tips, previous_micro_block.timestamp, block.tips, block.number_batch_blocks);
        // Note: if building the last micro block, GetTipsFast still works because previous epoch's request block tips
        // aren't connected to the current epoch's request block chain yet
        // if building the first micro block, the two request block chains will have already been linked at
        // epoch persistence time (happened at roughly one MB interval ago)
    }

    // should be allowed to have no blocks so at least it doesn't crash
    // for instance the node is disconnected for a while
    // (block._number_batch_blocks != 0);

    // should it be allowed to have no tips? same as above, i.e disconnected for a while
    // (std::count(block._tips.begin(), block._tips.end(), 0) == 0);

    ApprovedEB epoch;
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
    block.primary_delegate = 0xff;//epoch_handler does not know the delegate index which could change after every epoch transition
    block.sequence = first_micro_block
                     ? 0
                     : previous_micro_block.sequence + 1;
    block.last_micro_block = last_micro_block;

    LOG_INFO(_log) << "MicroBlockHandler::Build, built microblock:"
                   << " hash " << Blake2bHash<MicroBlock>(block).to_string()
                   << " timestamp " << block.timestamp
                   << " previous " << block.previous.to_string()
                   << " epoch_number " << block.epoch_number
                   << " primary " << (int)block.primary_delegate
                   << " sequence " << block.sequence
                   << " last_micro_block " << (int)block.last_micro_block;

    return true;
}