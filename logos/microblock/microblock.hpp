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
        , account(0)
        , epoch_number(0)
        , sequence(0)
        , last_micro_block(0)
        , number_batch_blocks(0)
        , tips{0}
        , next(0)
        , signature{0}
        {
            previous = 0;
        }

    /// Calculate block's hash
    BlockHash Hash() const;

    /// Overide to mirror state_block
    BlockHash hash() const { return Hash(); }

    /// JSON representation of MicroBlock (primarily for RPC messages)
    std::string SerializeJson() const;
    void SerializeJson(boost::property_tree::ptree &) const;

    static const size_t HASHABLE_BYTES;         ///< hashable bytes of the micrblock - used in signing
    logos::account      account; 	            ///< Delegate who proposed this microblock
    uint32_t            epoch_number; 			///< Current epoch
    uint16_t            sequence;	            ///< Microblock number within this epoch
    uint8_t             last_micro_block;       ///< The last microblock in the epoch
    uint8_t             padding1 = 0;           ///< padding
    uint32_t            number_batch_blocks;    ///< Number of batch blocks in the microblock
    uint32_t            padding2 = 0;           ///< padding
    BlockHash           tips[NUM_DELEGATES];    ///< Delegate's batch block tips
    BlockHash           next;                   ///< Next block reference
    Signature           signature; 		        ///< Multisignature
};
