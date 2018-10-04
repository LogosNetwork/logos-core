/// @file
/// This file contains the definition of the MicroBlockHandler class, which is used
/// in the Microblock processing
#include <logos/microblock/microblock_handler.hpp>
#include <logos/microblock/microblock_tester.hpp>
#include <logos/blockstore.hpp>
#include <logos/node/node.hpp>
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
            BOOST_LOG(_log) << "MicroBlockHander::BatchBlocksIterator failed to get batch state block: " <<
                hash.to_string();
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
    uint64_t min_timestamp = GetStamp() + CLOCK_DRIFT * 1000;

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
    uint64_t min_timestamp = GetStamp() + CLOCK_DRIFT * 1000;

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
        BOOST_LOG(_log) << "MicroBlockHandler::Build failed to get micro block tip";
        return false;
    }
    if (_store.micro_block_get(previous_micro_block_hash, previous_micro_block))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::Build failed to get micro block: " <<
            previous_micro_block_hash.to_string();
        return false;
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
    if (previous_micro_block._epoch_number == GENESIS_EPOCH)
    {
        GetTipsSlow(start, previous_micro_block._tips, block._tips,
                block._number_batch_blocks);
    }
    else
    {
        GetTipsFast(start, previous_micro_block._tips, block._tips,
                block._number_batch_blocks, previous_micro_block.timestamp);
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
        BOOST_LOG(_log) << "MicroBlockHandler::Build failed to get epoch tip";
        return false;
    }

    if (_store.epoch_get(hash, epoch))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::Build failed to get epoch: " <<
            hash.to_string();
        return false;
    }

    // first micro block in this epoch
    bool first_micro_block = epoch._micro_block_tip == previous_micro_block_hash;

    block._epoch_number = first_micro_block
            ? previous_micro_block._epoch_number + 1
            : previous_micro_block._epoch_number;
    block.previous = previous_micro_block_hash;
    block.timestamp = GetStamp();
    block._delegate = genesis_delegates[_delegate_id].key.pub;
    block._micro_block_number = first_micro_block
            ? 0
            : previous_micro_block._micro_block_number + 1;
    block._last_micro_block = last_micro_block;

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
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock micro block exists";
        return true;
    }

    // Account exists
    logos::account_info info;
    if (_store.account_get(block._delegate, info))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock account doesn't exist " <<
            block._delegate.to_account();
        return false;
    }

    Epoch previous_epoch;
    MicroBlock previous_microblock;

    // previous microblock doesn't exist
    if (_store.micro_block_get(block.previous, previous_microblock))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock previous doesn't exist " << block.previous.to_string();
        return false;
    }

    if (_store.epoch_tip_get(hash))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock failed to get epoch tip";
        return false;
    }

    if (_store.epoch_get(hash, previous_epoch))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock failed to get epoch: " <<
            hash.to_string();
        return false;
    }

    // previous and proposed microblock are in the same epoch
    if (block._epoch_number == previous_microblock._epoch_number)
    {
        if (block._micro_block_number != (previous_microblock._micro_block_number + 1))
        {
            BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock epoch number failed epoch #:" <<
                            block._epoch_number << " block #:" << block._micro_block_number <<
                            " previous block #:" << previous_microblock._micro_block_number;
            return false;
        }
    }
    // proposed microblock must be in new epoch
    else if (block._epoch_number != (previous_microblock._epoch_number + 1) ||
            block._micro_block_number != 0)
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock epoch number failed epoch #:" << block._epoch_number <<
            " previous block epoch #:" << previous_microblock._epoch_number <<
            " block #:" << block._micro_block_number;
        return false;
    }

    // timestamp should be equal to the cutoff interval plus allowed clock drift
    // unless it's the first microblock after genesis
    // Except if it is a recall
    int tdiff = ((int64_t)block.timestamp - (int64_t)previous_microblock.timestamp)/1000 -
            MICROBLOCK_CUTOFF_TIME * 60; //sec
    bool is_test_network = (logos::logos_network == logos::logos_networks::logos_test_network);
    if (!is_test_network && (previous_epoch._epoch_number != GENESIS_EPOCH || block._micro_block_number > 0) &&
            (!_recall_handler.IsRecall() && abs(tdiff) > CLOCK_DRIFT ||
             _recall_handler.IsRecall() && block.timestamp <= previous_microblock.timestamp))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock bad timestamp block ts:" << block.timestamp <<
            " previous block ts:" << previous_microblock.timestamp << " tdiff: " << tdiff <<
            " epoch # : " << block._epoch_number << " microblock #: " << block._micro_block_number;
        return false;
    }
    if (is_test_network)
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock WARNING: RUNNING WITH THE TEST FLAG ENABLED, "
                           "SOME VALIDATION IS DISABLED";
    }

    /// verify can iterate the chain and the number of blocks checks out
    int number_batch_blocks = 0;
    BatchBlocksIterator(block._tips, previous_microblock._tips, [&number_batch_blocks](uint8_t, const BatchStateBlock &) mutable -> void {
       ++number_batch_blocks;
    });
    if (number_batch_blocks != block._number_batch_blocks)
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock number of batch blocks doesn't match in block: " <<
                        block._number_batch_blocks << " to database: " << number_batch_blocks;
        return false;
    }

    /*BlockHash merkle_root = merkle::MerkleHelper([&](function<void(const BlockHash&)> cb)->void{
        BatchBlocksIterator(block._tips, previous_microblock._tips, [&](uint8_t delegate, const BatchStateBlock &batch)->void{
            cb(batch.Hash());
        });
    });

    if (merkle_root != block._merkle_root)
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock merkle root failed " << merkle_root.to_string() <<
                        " previous " << previous_microblock._merkle_root.to_string();
        return false;
    }*/

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
    BOOST_LOG(_log) << "MicroBlockHandler::ApplyUpdates hash: " << hash.to_string();
    return hash;
}
