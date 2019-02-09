/// @file
/// This file contains the declaration of the MicroBlock classe, which is used
/// in the Microblock processing
#pragma once
#include <logos/consensus/messages/common.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/lib/merkle.hpp>
#include <functional>

/// Microblocks are used for checkpointing and bootstrapping.
struct MicroBlock : PrePrepareCommon
{
    MicroBlock()
        : PrePrepareCommon()
        , last_micro_block(0)
        , number_batch_blocks(0)
        , tips()
    {}

    MicroBlock(bool & error, logos::stream & stream, bool with_appendix)
    : PrePrepareCommon(error, stream)
    {
        if(error)
        {
            return;
        }

        error = logos::read(stream, last_micro_block);
        if(error)
        {
            return;
        }

        error = logos::read(stream, number_batch_blocks);
        if(error)
        {
            return;
        }
        number_batch_blocks = le32toh(number_batch_blocks);

        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            error = logos::read(stream, tips[i]);
            if(error)
            {
                return;
            }
        }
    }

    void Hash(blake2b_state & hash) const
    {
        uint32_t nbb = htole32(number_batch_blocks);
        PrePrepareCommon::Hash(hash, true);
        blake2b_update(&hash, &last_micro_block, sizeof(uint8_t));
        blake2b_update(&hash, &nbb, sizeof(uint32_t));
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            blake2b_update(&hash, tips[i].data(), HASH_SIZE);
        }
    }

    /// JSON representation of MicroBlock (primarily for RPC messages)
    std::string SerializeJson() const;
    void SerializeJson(boost::property_tree::ptree &) const;
    uint32_t Serialize(logos::stream & stream, bool with_appendix) const;

    uint8_t             last_micro_block;       ///< The last microblock in the epoch
    uint32_t            number_batch_blocks;    ///< Number of batch blocks in the microblock
    BlockHash           tips[NUM_DELEGATES];    ///< Delegate's batch block tips
};
