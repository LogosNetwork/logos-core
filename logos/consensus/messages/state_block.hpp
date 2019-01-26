///
/// @file
/// This file contains declaration and implementation of ReceiveBlock and StateBlock
///
#pragma once

#include <logos/consensus/messages/common.hpp>
#include <logos/node/utility.hpp>
#include <logos/lib/hash.hpp>
#include <logos/lib/log.hpp>

#include <vector>

#include <blake2/blake2.h>
#include <ed25519-donna/ed25519.h>

/// An item on the receive chain of an account. A ReceiveBlock is created for each transaction is a StateBlock
struct ReceiveBlock
{
    BlockHash previous;
    BlockHash send_hash;
    uint16_t index2send = 0;

    ReceiveBlock() = default;

    /// Class constructor
    /// @param previous the hash of the previous ReceiveBlock on the account chain
    /// @param send_hash the hash of the StateBlock
    /// @param index2send the index to the array of transactions in the StateBlock
    ReceiveBlock(const BlockHash & previous, const BlockHash & send_hash, uint16_t index2send = 0)
    : previous(previous), send_hash(send_hash), index2send(index2send)
    {}

    /// Constructor from deserializing a buffer read from the database
    /// @param error it will be set to true if deserialization fail [out]
    /// @param mdbval the buffer read from the database
    ReceiveBlock(bool & error, const logos::mdb_val & mdbval)
    {
        logos::bufferstream stream(reinterpret_cast<uint8_t const *> (mdbval.data()), mdbval.size());

        error = logos::read(stream, previous);
        if(error)
        {
            return;
        }

        error = logos::read(stream, send_hash);
        if(error)
        {
            return;
        }

        error = logos::read(stream, index2send);
        if(error)
        {
            return;
        }
        index2send= le16toh(index2send);
    }

    /// Serialize the data members to a Json string
    /// @returns the Json string
    std::string SerializeJson() const
    {
        boost::property_tree::ptree tree;
        SerializeJson (tree);
        std::stringstream ostream;
        boost::property_tree::write_json(ostream, tree);
        return ostream.str();
    }

    /// Add the data members to the property_tree which will be encoded to Json
    /// @param batch_state_block the property_tree to add data members to
    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        tree.put("previous", previous.to_string());
        tree.put("send_hash", send_hash.to_string());
        tree.put("index_to_send_block", std::to_string(index2send));
    }

    /// Serialize the data members to a stream
    /// @param stream the stream to serialize to
    void Serialize (logos::stream & stream) const
    {
        uint16_t idx = htole16(index2send);

        logos::write(stream, previous);
        logos::write(stream, send_hash);
        logos::write(stream, idx);
    }

    /// Compute the hash of the ReceiveBlock
    /// @returns the hash value computed
    BlockHash Hash() const
    {
        return Blake2bHash<ReceiveBlock>(*this);
    }

    /// Add the data members to a hash context
    /// @param hash the hash context
    void Hash(blake2b_state & hash) const
    {
        uint16_t s = htole16(index2send);
        previous.Hash(hash);
        send_hash.Hash(hash);
        blake2b_update(&hash, &s, sizeof(uint16_t));
    }

    /// Serialize the data members to a database buffer
    /// @param buf the memory buffer to serialize to
    /// @return the database buffer
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            Serialize(stream);
        }
        return logos::mdb_val(buf.size(), buf.data());
    }
};


