/// @file
/// This file contains the definition of the MicroBlockHandler class, which is used
/// in the Microblock processing
#include <logos/microblock/microblock_handler.hpp>
#include <logos/microblock/microblock_tester.hpp>
#include <logos/blockstore.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>
#include <time.h>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

using namespace logos;

void
MicroBlockHandler::BatchBlocksIterator(
    const BatchTips &start,
    const BatchTips &end,
    IteratorBatchBlockReceiverCb batchblock_receiver)
{
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        BlockHash hash = start[delegate];
        BatchStateBlock batch;
        bool not_found = false;
        for (not_found = _store.batch_block_get(hash, batch);
             !not_found && hash != end[delegate];
             hash = batch.previous, not_found = _store.batch_block_get(hash, batch)) {
            batchblock_receiver(delegate, batch);
        }
        if (not_found && hash != 0)
        {
            LOG_ERROR(_log) << "MicroBlockHander::BatchBlocksIterator failed to get batch state block: "
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
       BatchBlocksIterator(start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable -> void {
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
    BatchBlocksIterator(start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable->void{
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
    BatchBlocksIterator(start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable -> void {
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
    BatchBlocksIterator(start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable->void{
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
    block.account = NodeIdentityManager::_delegate_account;
    block.sequence = first_micro_block
            ? 0
            : previous_micro_block.sequence + 1;
    block.last_micro_block = last_micro_block;

    LOG_INFO(_log) << "MicroBlockHandler::Build, built microblock:"
                   << " hash " << block.Hash().to_string()
                   << " timestamp " << block.timestamp
                   << " previous " << block.previous.to_string()
                   << " epoch_number " << block.epoch_number
                   << " account " << block.account.to_account()
                   << " sequence " << block.sequence
                   << " last_micro_block " << (int)block.last_micro_block;

    return true;
}

bool
MicroBlockHandler::Validate(
        const MicroBlock &block)
{
    BlockHash hash = block.Hash();

    // block exists
    if (_store.micro_block_exists(hash))
    {
        LOG_WARN(_log) << "MicroBlockHandler::VerifyMicroBlock micro block exists";
        return true;
    }

    // Account exists
    logos::account_info info;
    if (_store.account_get(block.account, info))
    {
        LOG_ERROR(_log) << "MicroBlockHandler::VerifyMicroBlock account doesn't exist "
                        << block.account.to_account();
        return false;
    }

    Epoch previous_epoch;
    MicroBlock previous_microblock;

    // previous microblock doesn't exist
    if (_store.micro_block_get(block.previous, previous_microblock))
    {
        LOG_ERROR(_log) << "MicroBlockHandler::VerifyMicroBlock previous doesn't exist "
                        << block.previous.to_string();
        return false;
    }

    if (_store.epoch_tip_get(hash))
    {
        LOG_FATAL(_log) << "MicroBlockHandler::VerifyMicroBlock failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(hash, previous_epoch))
    {
        LOG_FATAL(_log) << "MicroBlockHandler::VerifyMicroBlock failed to get epoch: "
                        << hash.to_string();
        trace_and_halt();
    }

    // previous and proposed microblock are in the same epoch
    if (block.epoch_number == previous_microblock.epoch_number)
    {
        if (block.sequence != (previous_microblock.sequence + 1))
        {
            LOG_ERROR(_log) << "MicroBlockHandler::VerifyMicroBlock epoch number failed epoch #:"
                            << block.epoch_number << " block #:" << block.sequence
                            << " previous block #:" << previous_microblock.sequence;
            return false;
        }
    }
    // proposed microblock must be in new epoch
    else if (block.epoch_number != (previous_microblock.epoch_number + 1) ||
            block.sequence != 0)
    {
        LOG_ERROR(_log) << "MicroBlockHandler::VerifyMicroBlock epoch number failed epoch #:"
                        << block.epoch_number << " previous block epoch #:"
                        << previous_microblock.epoch_number << " block #:" << block.sequence;
        return false;
    }

    // timestamp should be equal to the cutoff interval plus allowed clock drift
    // unless it's the first microblock after genesis
    // Except if it is a recall
    int tdiff = ((int64_t)block.timestamp - (int64_t)previous_microblock.timestamp)/1000 -
            TConvert<Seconds>(MICROBLOCK_CUTOFF_TIME).count(); //sec
    bool is_test_network = (logos::logos_network == logos::logos_networks::logos_test_network);
    if (!is_test_network && (previous_epoch.epoch_number != GENESIS_EPOCH || block.sequence > 0) &&
            (!_recall_handler.IsRecall() && abs(tdiff) > CLOCK_DRIFT.count() ||
             _recall_handler.IsRecall() && block.timestamp <= previous_microblock.timestamp))
    {
        LOG_ERROR(_log) << "MicroBlockHandler::VerifyMicroBlock bad timestamp block ts:" << block.timestamp
                        << " previous block ts:" << previous_microblock.timestamp << " tdiff: " << tdiff
                        << " epoch # : " << block.epoch_number << " microblock #: " << block.sequence;
        return false;
    }
    if (is_test_network)
    {
        LOG_WARN(_log) << "MicroBlockHandler::VerifyMicroBlock WARNING: RUNNING WITH THE TEST FLAG ENABLED, "
                           "SOME VALIDATION IS DISABLED";
    }

    /// verify can iterate the chain and the number of blocks checks out
    int number_batch_blocks = 0;
    BatchBlocksIterator(block.tips, previous_microblock.tips, [&number_batch_blocks](uint8_t, const BatchStateBlock &) mutable -> void {
       ++number_batch_blocks;
    });
    if (number_batch_blocks != block.number_batch_blocks)
    {
        LOG_ERROR(_log) << "MicroBlockHandler::VerifyMicroBlock number of batch blocks doesn't match in block: "
                        << block.number_batch_blocks << " to database: " << number_batch_blocks;
        return false;
    }

    // verify can get the batch block tips
    BatchStateBlock bsb;
    for (int del = 0; del < NUM_DELEGATES; ++del)
    {
        if (block.tips[del] != 0 && _store.batch_block_get(block.tips[del], bsb))
        {
            LOG_ERROR(_log) << "MicroBlockHandler::VerifyMicroBlock failed to get batch tip: "
                            << block.tips[del].to_string();
            return false;
        }
    }

    return true;
}

void
MicroBlockHandler::ApplyUpdates(const MicroBlock &block)
{
    logos::transaction transaction(_store.environment, nullptr, true);
    ApplyUpdates(block, transaction);
}

BlockHash
MicroBlockHandler::ApplyUpdates(const MicroBlock &block, const logos::transaction &transaction)
{

    BlockHash hash = _store.micro_block_put(block, transaction);
    _store.micro_block_tip_put(hash, transaction);
    LOG_INFO(_log) << "MicroBlockHandler::ApplyUpdates hash: " << hash.to_string();
    return hash;
}
