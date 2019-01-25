///
/// @file
/// This file contains declaration of the BatchStateBlock
///
#pragma once

#include <logos/consensus/messages/state_block.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/request/send.hpp>


using BlockList     = Send [CONSENSUS_BATCH_SIZE];
using BlockHashList = BlockHash [CONSENSUS_BATCH_SIZE];

struct BatchStateBlock : PrePrepareCommon
{
    BatchStateBlock() = default;

    /// Class constructor
    /// construct from deserializing a stream of bytes
    /// @param error it will be set to true if deserialization fail [out]
    /// @param stream the stream containing serialized data [in]
    /// @param with_state_block if the serialized data have state blocks [in]
    BatchStateBlock(bool & error, logos::stream & stream, bool with_state_block);

    /// Add a new state block
    /// @param to_add the new state block to be added
    /// @returns if the new state block is added.
    bool AddStateBlock(const Send & to_add)
    {
        if(block_count >= CONSENSUS_BATCH_SIZE)
            return false;

        new(&blocks[block_count]) Send(to_add);
        hashs[block_count] = blocks[block_count].GetHash();
        ++block_count;
        return true;
    }

    /// Add the data members to a hash context
    /// @param hash the hash context
    void Hash(blake2b_state & hash) const
    {
        uint16_t bc = htole16(block_count);

        PrePrepareCommon::Hash(hash);
        blake2b_update(&hash, &bc, sizeof(uint16_t));
        for(uint16_t i = 0; i < block_count; ++i)
        {
            hashs[i].Hash(hash);
        }
    }

    /// Add the data members to the property_tree which will be encoded to Json
    /// @param batch_state_block the property_tree to add data members to
    void SerializeJson(boost::property_tree::ptree &batch_state_block) const;

    /// Serialize the data members to a stream
    /// @param stream the stream to serialize to
    /// @param with_state_block if the state blocks should be serialize
    /// @returns the number of bytes serialized
    uint32_t Serialize(logos::stream & stream, bool with_state_block) const;

    uint16_t        block_count  = 0;
    BlockList       blocks;
    BlockHashList   hashs;
};

