/// @file
/// This file contains the definition of the MicroBlock classe, which is used
/// in the Microblock processing
#include <logos/microblock/microblock.hpp>

const size_t MicroBlock::HASHABLE_BYTES = sizeof(MicroBlock)
                                            - sizeof(Signature);

BlockHash
MicroBlock::Hash() const {
    return merkle::Hash([&](std::function<void(const void *data, size_t)> cb)mutable -> void {
        //cb(&timestamp, sizeof(timestamp));
        cb(previous.bytes.data(), sizeof(previous));
        //cb(&_delegate, sizeof(_delegate));
        cb(&epoch_number, sizeof(epoch_number));
        cb(&sequence, sizeof(sequence));
        cb(&number_batch_blocks, sizeof(number_batch_blocks));
        cb(tips, NUM_DELEGATES * sizeof(BlockHash));
    });
}