///
/// @file
/// This file contains the declaration of Merkle tree related functions
///
#pragma once

#include <blake2/blake2.h>
#include <logos/lib/numbers.hpp>
#include <functional>
#include <vector>
using namespace std;

namespace merkle {

    using HashUpdaterCb         = function<void(const void *, size_t)>;
    using HashDataProviderCb    = function<void(HashUpdaterCb)>;
    using HashReceiverCb     = function<void(const logos::block_hash&)>;
    using HashIteratorProviderCb= function<void(HashReceiverCb)>;

    /// Calculate blake2 hash.
    /// The caller has to pass in HashDataProviderCb. Hash passes HashUpdaterCb to the
    /// HashDataProviderCb. Data provider repeatedly calls HashUpdaterCb on a data, where
    /// the data is represented by the void* and size_t. After the HashDataProviderCb
    /// returns, Hash function finalizes calculation of the hash.
    /// @param data_provider is a function that takes HashUpdaterCb
    /// @returns the hash
    logos::block_hash Hash(HashDataProviderCb data_provider);

    /// Blake2 hash, calculates the hash of two hashes
    /// @param h1 hash
    /// @param h2 hash
    /// @returns the hash
    logos::block_hash Hash(const logos::block_hash &h1, const logos::block_hash &h2);

    /// MerkleRoot calculation
    /// @param merkle [in,out] is the vector of leaf nodes or any level of parent nodes,
    ///    overwritten on return
    /// @returns Merkle tree root
    logos::block_hash MerkleRoot(vector<logos::block_hash> &merkle);

    /// Merkle Root calculation helper function. The caller has to pass in HashIteratorProviderCb.
    /// The helper passes ElementReceiverCb to the HashIteratorProviderCb.
    /// HashIteratorProviderCb repeatedly calls ElementReceiverCb for
    /// each element of it' data. ElementReceiverCb uses the data to pre-calculate first level of the
    /// merkle tree nodes. Once iteration completes, the helper calculates the rest of the tree and the tree's root.
    /// @param iterator_provider is a function that takes ElementReceiverCb as an argument
    /// @returns Merkle tree root
    logos::block_hash
    MerkleHelper(HashIteratorProviderCb iterator_provider);
}