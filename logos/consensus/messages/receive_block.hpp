///
/// @file
/// This file contains declaration and implementation of ReceiveBlock
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
    ReceiveBlock() = default;

    /// Class constructor
    /// @param previous the hash of the previous ReceiveBlock on the account chain
    /// @param send_hash the hash of the StateBlock
    /// @param index2send the index to the array of transactions in the StateBlock
    ReceiveBlock(const BlockHash & previous,
                 const BlockHash & send_hash,
                 uint16_t index2send = 0);

    /// Constructor from deserializing a buffer read from the database
    /// @param error it will be set to true if deserialization fail [out]
    /// @param mdbval the buffer read from the database
    ReceiveBlock(bool & error, const logos::mdb_val & mdbval);

    /// Serialize the data members to a Json string
    /// @returns the Json string
    std::string ToJson() const;

    /// Add the data members to the property_tree which will be encoded to Json
    /// @param batch_state_block the property_tree to add data members to
    void SerializeJson(boost::property_tree::ptree & tree) const;
    boost::property_tree::ptree SerializeJson() const;

    /// Serialize the data members to a stream
    /// @param stream the stream to serialize to
    void Serialize (logos::stream & stream) const;

    /// Compute the hash of the ReceiveBlock
    /// @returns the hash value computed
    BlockHash Hash() const;

    /// Add the data members to a hash context
    /// @param hash the hash context
    void Hash(blake2b_state & hash) const;

    /// Serialize the data members to a database buffer
    /// @param buf the memory buffer to serialize to
    /// @return the database buffer
    logos::mdb_val to_mdb_val(std::vector<uint8_t> &buf) const;

    BlockHash previous;
    BlockHash send_hash;
    uint16_t  index2send = 0;
};


