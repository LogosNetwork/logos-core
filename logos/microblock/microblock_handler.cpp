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
MicroBlockHandler::WalkBatchBlocks(
    const BatchTips &start,
    const BatchTips &end,
    std::function<void(uint8_t, const BatchStateBlock&)> cb)
{
    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        BlockHash hash = start[delegate];
        BatchStateBlock batch;
        for (bool not_found = _store.batch_block_get(hash, batch);
             !not_found && hash != end[delegate];
             hash = batch.previous, not_found = _store.batch_block_get(hash, batch)) {
            cb(delegate, batch);
        }
    }
}

/// If the previous microblock has the time stamp (any microblock after the genesis block)
/// then can just walk the batch blocks and select the onces that are less than the
/// cutoff time
BlockHash
MicroBlockHandler::FastMerkleTree(
    const BatchTips &start,
    const BatchTips &end,
    BatchTips &tips,
    uint &num_blocks,
    uint64_t timestamp)
{
   uint64_t cutoff_msec = timestamp + MICROBLOCK_CUTOFF_TIME * 60 *1000;
   return MerkleHelper([&](function<void(const BlockHash&)> cb)->void {
       WalkBatchBlocks(start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable -> void {
          if (batch.timestamp < cutoff_msec)
          {
              BlockHash hash = batch.Hash();
              if (tips[delegate] == 0)
              {
                  tips[delegate] = hash;
              }
              num_blocks++;
              cb(hash);
          }
       });
   });
}

/// The first after genesis microblock doesn't have the previous time stamp reference
/// because genesis blocks have to have 0 timestamp so all nodes have the same hash
/// Therefore the approach is to collect all batch blocks and use the oldest time stamp
/// as the reference. Then filter collected batch blocks based on the time stamp
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
    uint64_t min_timestamp = GetStamp() + LOCAL_CLOCK_DRIFT * 1000;

    // frist get hashes and timestamps of all blocks; and min timestamp to use as the base
    WalkBatchBlocks(start, end, [&](uint8_t delegate, const BatchStateBlock &batch)mutable->void{
       entries[delegate].push_back({batch.timestamp, batch.Hash()});
       if (batch.timestamp < min_timestamp)
       {
           min_timestamp = batch.timestamp;
       }
    });

    // iterate over all blocks, selecting the ones that less then cutoff time
    // and calcuate the merkle root with the MerkleHelper
    uint64_t cutoff_msec = min_timestamp + MICROBLOCK_CUTOFF_TIME * 60 *1000;

    return MerkleHelper([&](function<void(const BlockHash&)> cb)->void {
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
                cb(it.hash);
            }
        }
    });
}

/// Microblock cut off time is calculated as Tc = TEi + Mi * 10 where TEi is the i-th epoch (previous epoch),
/// Mi is current microblock sequence number
bool
MicroBlockHandler::BuildMicroBlock(
    MicroBlock &block,
    bool last_micro_block)
{
    BlockHash previous_micro_block_hash;
    MicroBlock previous_micro_block;

    assert(false == _store.micro_block_tip_get(previous_micro_block_hash));
    assert(false == _store.micro_block_get(previous_micro_block_hash, previous_micro_block));

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
        block._merkle_root = SlowMerkleTree(start, previous_micro_block._tips, block._tips,
                block._number_batch_blocks);
    }
    else
    {
        block._merkle_root = FastMerkleTree(start, previous_micro_block._tips, block._tips,
                block._number_batch_blocks, previous_micro_block.timestamp);
    }

    // should be allowed to have no blocks so at least it doesn't crash
    // for instance the node is disconnected for a while
    //assert (block._number_batch_blocks != 0);

    // should it be allowed to have no tips? same as above, i.e disconnected for a while
    //assert(std::count(block._tips.begin(), block._tips.end(), 0) == 0);

    Epoch epoch;
    BlockHash hash;
    assert(false == _store.epoch_tip_get(hash));
    assert(false == _store.epoch_get(hash, epoch));
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
    BOOST_LOG(_log) << "MicroBlockHandler::BuildMicroBlock SIGNING MICROBLOCK WITH DELEGATE ID " <<
        (int)_delegate_id << " THIS WILL NOT WORK DURING TRANSITION BECAUSE DELEGATE ID WILL BE DUPLICATE";
    block._signature = logos::sign_message(genesis_delegates[_delegate_id].key.prv,
            genesis_delegates[_delegate_id].key.pub, block.Hash());

    return true;
}

bool
MicroBlockHandler::VerifyMicroBlock(
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

    Epoch current_epoch;
    MicroBlock previous_microblock;

    // previous microblock doesn't exist
    if (_store.micro_block_get(block.previous, previous_microblock))
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock previous doesn't exist " << block.previous.to_string();
        return false;
    }

    assert(false == _store.epoch_tip_get(hash));
    assert(false == _store.epoch_get(hash, current_epoch));

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
    if (!is_test_network && (current_epoch._epoch_number != GENESIS_EPOCH || block._micro_block_number > 0) &&
            (!_recall_handler.IsRecall() && abs(tdiff) > LOCAL_CLOCK_DRIFT ||
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

    BlockHash merkle_root = MerkleHelper([&](function<void(const BlockHash&)> cb)->void{
        WalkBatchBlocks(block._tips, previous_microblock._tips, [&](uint8_t delegate, const BatchStateBlock &batch)->void{
            cb(batch.Hash());
        });
    });

    if (merkle_root != block._merkle_root)
    {
        BOOST_LOG(_log) << "MicroBlockHandler::VerifyMicroBlock merkle root failed " << merkle_root.to_string() <<
                        " previous " << previous_microblock._merkle_root.to_string();
        return false;
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
    BOOST_LOG(_log) << "MicroBlockHandler::ApplyUpdates hash: " << hash.to_string();
    return hash;
}
