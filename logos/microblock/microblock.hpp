/// @file
/// This file contains the declaration of the MicroBlock classe, which is used
/// in the Microblock processing
#pragma once
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/lib/merkle.hpp>
#include <functional>

using BlockHash = logos::block_hash;

/// Microblocks are used for checkpointing and bootstrapping.
struct MicroBlock : MessageHeader<MessageType::Pre_Prepare, ConsensusType::MicroBlock> {
    MicroBlock()
        : MessageHeader(0)
        , _merkle_root(0)
        , _delegate(0)
        , _epoch_number(0)
        , _micro_block_number(0)
        , _last_micro_block(0)
        , _tips{0}
        , _number_batch_blocks(0)
        {
            signature={0};
            previous = 0;
        }

    /// Calculate block's hash
    BlockHash Hash() const;

    /// Overide to mirror state_block
    BlockHash hash() const { return Hash(); }
    static const size_t HASHABLE_BYTES;         ///< hashable bytes of the micrblock - used in signing
    BlockHash           _merkle_root; 		    ///< Merkle root of the batch blocks included in this microblock
    logos::account      _delegate; 	            ///< Delegate who proposed this microblock
    uint                _epoch_number; 			///< Current epoch
    uint16_t            _micro_block_number;	///< Microblock number within this epoch
    uint8_t             _last_micro_block;      ///< The last microblock in the epoch
    BlockHash           _tips[NUM_DELEGATES];   ///< Delegate's batch block tips
    uint                _number_batch_blocks;   ///< Number of batch blocks in the microblock
    Signature           signature; 		        ///< Multisignature
};
