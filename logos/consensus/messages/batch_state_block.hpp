#ifndef LOGOS_CONSENSUS_MESSAGES_BATCH_STATE_BLOCK_HPP_
#define LOGOS_CONSENSUS_MESSAGES_BATCH_STATE_BLOCK_HPP_

#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/state_block.hpp>


using BlockList             = StateBlock [CONSENSUS_BATCH_SIZE];
using BlockHashList         = BlockHash [CONSENSUS_BATCH_SIZE];

struct BatchStateBlock : PrePrepareCommon
{
    BatchStateBlock() = default;
    BatchStateBlock(bool & error, logos::stream & stream, bool with_state_block);

    bool AddStateBlock(const StateBlock & to_add)
    {
        if(block_count >= CONSENSUS_BATCH_SIZE)
            return false;

        new(&blocks[block_count]) StateBlock(to_add);
        hashs[block_count] = blocks[block_count].GetHash();
        ++block_count;
        return true;
    }

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

    void SerializeJson(boost::property_tree::ptree &) const;
    uint32_t Serialize(logos::stream & stream, bool with_state_block) const;

    uint16_t        block_count  = 0;
    BlockList       blocks;
    BlockHashList   hashs;
};

#endif /* LOGOS_CONSENSUS_MESSAGES_BATCH_STATE_BLOCK_HPP_ */
