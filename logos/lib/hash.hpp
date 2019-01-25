#pragma once

#include <blake2/blake2.h>

using BlockHash = logos::uint256_union;

template<typename T>
BlockHash Blake2bHash(const T & t)
{
    BlockHash digest;
    blake2b_state hash;

    auto status(blake2b_init(&hash, sizeof(BlockHash)));
    assert(status == 0);

    t.Hash(hash);

    status = blake2b_final(&hash, digest.data(), sizeof(BlockHash));
    assert(status == 0);

    return digest;
}
