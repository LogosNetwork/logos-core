///
/// @file
/// This file contains declaration of the RequestBlock
///
#pragma once

#include <logos/consensus/messages/receive_block.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/request/send.hpp>

struct RequestBlock : PrePrepareCommon
{
    using RequestPtr      = std::shared_ptr<Request>;
    using RequestList     = std::vector<RequestPtr>;
    using RequestHashList = std::vector<BlockHash>;

    RequestBlock() = default;

    /// Class constructor
    /// construct from deserializing a stream of bytes
    /// @param error it will be set to true if deserialization fail [out]
    /// @param stream the stream containing serialized data [in]
    /// @param with_requests if the serialized data have state blocks [in]
    RequestBlock(bool & error, logos::stream & stream, bool with_requests);

    // TODO: Possibly preserve shared_ptr from rpc request
    //

    /// Add a new request
    /// @param request the new request to be added
    /// @returns true if the new request was added.
    bool AddRequest(RequestPtr request);

    /// Add the data members to a hash context
    /// @param hash the hash context
    void Hash(blake2b_state & hash) const;

    /// Add the data members to the property_tree which will be encoded to Json
    /// @param tree the property_tree to add data members to
    void SerializeJson(boost::property_tree::ptree & tree) const;

    /// Serialize the data members to a stream
    /// @param stream the stream to serialize to
    /// @param with_requests if the state blocks should be serialize
    /// @returns the number of bytes serialized
    uint32_t Serialize(logos::stream & stream, bool with_requests) const;

    RequestList     requests;
    RequestHashList hashes;
};

