#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/common.hpp>

#ifndef BOOST_LITTLE_ENDIAN
    static_assert(false, "Only LITTLE_ENDIAN machines are supported!");
#endif

constexpr size_t P2pConsensusHeader::P2PHEADER_SIZE;

RequestBlock::RequestBlock(bool & error, logos::stream & stream, bool with_state_block)
    : PrePrepareCommon(error, stream)
{
    if(error)
    {
        return;
    }

    error = logos::read(stream, block_count);
    if(error)
    {
        return;
    }
    block_count = le16toh(block_count);
    if(block_count > CONSENSUS_BATCH_SIZE)
    {
        error = true;
        return;
    }

    hashes.reserve(block_count);
    for(uint64_t i = 0; i < block_count; ++i)
    {
        BlockHash new_hash;
        error = logos::read(stream, new_hash);
        if(error)
        {
            return;
        }
        hashes.emplace_back(new_hash);
     }

    if( with_state_block )
    {
        blocks.reserve(block_count);
        for(uint64_t i = 0; i < block_count; ++i)
        {
            new(&blocks[i]) Send(error, stream);
            if(error)
            {
                return;
            }
        }
    }
}

void RequestBlock::SerializeJson(boost::property_tree::ptree & batch_state_block) const
{
    PrePrepareCommon::SerializeJson(batch_state_block);

    batch_state_block.put("type", "RequestBlock");
    batch_state_block.put("block_count", std::to_string(block_count));

    boost::property_tree::ptree blocks_tree;
    for(uint64_t i = 0; i < block_count; ++i)
    {
        boost::property_tree::ptree txn_content = blocks[i].SerializeJson();
        blocks_tree.push_back(std::make_pair("", txn_content));
    }
    batch_state_block.add_child("blocks", blocks_tree);
}

uint32_t RequestBlock::Serialize(logos::stream & stream, bool with_state_block) const
{
    uint16_t bc = htole16(block_count);

    auto s = PrePrepareCommon::Serialize(stream);
    s += logos::write(stream, bc);

    for(uint64_t i = 0; i < block_count; ++i)
    {
        s += logos::write(stream, hashes[i]);
    }

    if(with_state_block)
    {
        for(uint64_t i = 0; i < block_count; ++i)
        {
            s += blocks[i].Serialize(stream);
        }
    }

    return s;
}

ConnectedClientIds::ConnectedClientIds(uint32_t epoch_number,
                                       uint8_t delegate_id,
                                       EpochConnection connection,
                                       const char *ip)
    : epoch_number(epoch_number)
    , delegate_id(delegate_id)
    , connection(connection)
{
    strncpy(this->ip, ip, INET6_ADDRSTRLEN);
}

ConnectedClientIds::ConnectedClientIds(bool & error, logos::stream & stream)
{
    error = logos::read(stream, epoch_number);
    if(error)
    {
        return;
    }
    epoch_number = le32toh(epoch_number);

    error = logos::read(stream, delegate_id);
    if(error)
    {
        return;
    }

    error = logos::read(stream, connection);
    if(error)
    {
        return;
    }

    error = logos::read(stream, ip);
}

uint32_t ConnectedClientIds::Serialize(std::vector<uint8_t> & buf) const
{
    assert(buf.empty());
    logos::vectorstream stream(buf);

    auto s = logos::write(stream, htole32(epoch_number));
    s += logos::write(stream, delegate_id);
    s += logos::write(stream, connection);
    s += logos::write(stream, ip);

    assert(StreamSize() == s);
    return s;
}

void UpdateNext(const logos::mdb_val &mdbval, logos::mdb_val &mdbval_buf, const BlockHash &next)
{
    if(mdbval.size() <= HASH_SIZE)
    {
        Log log;
        LOG_FATAL(log) << __func__ << " DB value too small";
        trace_and_halt();
    }

    // From LMDB:
    //    The memory pointed to by the returned values is owned by the database.
    //    The caller need not dispose of the memory, and may not modify it in any
    //    way. For values returned in a read-only transaction any modification
    //    attempts will cause a SIGSEGV.
    //    Values returned from the database are valid only until a subsequent
    //    update operation, or the end of the transaction.

    memcpy(mdbval_buf.data(), mdbval.data(), mdbval_buf.size() - HASH_SIZE);
    uint8_t * start_of_next = reinterpret_cast<uint8_t *>(mdbval_buf.data()) + mdbval_buf.size() - HASH_SIZE;
    const uint8_t * next_data = next.data();
    memcpy(start_of_next, next_data, HASH_SIZE);
}
