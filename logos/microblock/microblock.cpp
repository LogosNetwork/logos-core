/// @file
/// This file contains the definition of the MicroBlock classe, which is used
/// in the Microblock processing
#include <logos/microblock/microblock.hpp>

const size_t MicroBlock::HASHABLE_BYTES = sizeof(MicroBlock)
                                            - sizeof(Signature);

BlockHash
MicroBlock::Hash() const {
    return merkle::Hash([&](std::function<void(const void *data, size_t)> cb)mutable -> void {
        cb(&timestamp, sizeof(timestamp));
        cb(previous.bytes.data(), sizeof(previous));
        //cb(_merkle_root.bytes.data(), sizeof(_merkle_root));
        cb(&_delegate, sizeof(_delegate));
        cb(&_epoch_number, sizeof(_epoch_number));
        cb(&_micro_block_number, sizeof(_micro_block_number));
        cb(_tips, NUM_DELEGATES * sizeof(BlockHash));
        cb(&_number_batch_blocks, sizeof(_number_batch_blocks));
    });
}