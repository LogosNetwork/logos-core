///
/// @file
/// This file contains the definition of Merkle tree related functions
///
#include <logos/lib/merkle.hpp>
#include <blake2/blake2.h>

namespace merkle {
    BlockHash
    Hash(HashDataProviderCb data_provider) {
      BlockHash result;
      blake2b_state hash_l;

      // initialize blake2b
      auto status(blake2b_init(&hash_l, sizeof(result)));
      assert (status == 0);

      // update the hash with the passed in data
      auto hash_updater = [&](const void *data, size_t size)mutable -> void {
          status = blake2b_update(&hash_l, data, size);
          assert (status == 0);
      };

      // call provided call back
      // the caller can repeatedly call 'update' to calcuate hash of 'any' daa
      data_provider(hash_updater);

      // finalize the hash
      status = blake2b_final(&hash_l, result.data(), sizeof result);
      assert (status == 0);

      return result;
    }

    BlockHash
    Hash(const BlockHash &h1, const BlockHash &h2) {
      return Hash([&](HashUpdaterCb hash_updater)mutable -> void {
          hash_updater(h1.data(), sizeof(h1));
          hash_updater(h2.data(), sizeof(h2));
      });
    }

    BlockHash
    MerkleRoot(
        vector<BlockHash> &merkle) {
      if (merkle.size() == 0) {
        return BlockHash();
      }
      while (merkle.size() > 1) {
        if (merkle.size() % 2) // make the number of nodes even
          merkle.push_back(merkle.back()); // add the last node
        for (int i = 0, j = 0; i < merkle.size(); i += 2, j++)
          merkle[j] = Hash(merkle[i], merkle[i + 1]);
        merkle.resize(merkle.size() / 2);
      }
      return merkle[0];
    }

    BlockHash
    MerkleHelper(
        HashIteratorProviderCb iterator_provider) {
      vector<BlockHash> merkle;
      BlockHash previous_hash;
      int cnt = 0;

      iterator_provider([&](const BlockHash &hash)mutable -> void {
          ++cnt;
          if (cnt % 2 == 0) {
            merkle.push_back(Hash(previous_hash, hash));
          } else {
            previous_hash = hash;
          }
      });

      if (cnt % 2) {
        merkle.push_back(Hash(previous_hash, previous_hash));
      }

      return MerkleRoot(merkle);
    }
}
