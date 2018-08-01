#include <rai/consensus/messages/messages.hpp>
#include <rai/common.hpp>

const size_t BatchStateBlock::HASHABLE_BYTES = sizeof(BatchStateBlock)
                                               - sizeof(Signature);

BlockHash BatchStateBlock::Hash() const
{
    rai::uint256_union result;
    blake2b_state hash;

    auto status(blake2b_init(&hash, sizeof(result.bytes)));
    assert(status == 0);

    blake2b_update(&hash, &block_count, sizeof(block_count));
    blake2b_update(&hash, blocks, sizeof(BlockList));

    status = blake2b_final(&hash, result.bytes.data(), sizeof(result.bytes));
    assert(status == 0);

    return result;
}
