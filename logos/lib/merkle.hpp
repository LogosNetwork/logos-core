///
/// @file
/// This file contains the declaration of Merkle tree related functions
///
#pragma once
#include <logos/lib/numbers.hpp>
#include <functional>
#include <vector>
using namespace std;

/// Blake2 hash, calculates the hash of any data passed in
/// @param cb is a function that passed in a hash update function
/// @returns the hash
logos::block_hash Hash(function<void(function<void(const void*, size_t)>)> cb);
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
