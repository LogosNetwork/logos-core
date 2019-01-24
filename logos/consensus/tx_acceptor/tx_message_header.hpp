// @file
// TxMessageHeader declares TxAcceptor message header
//

#pragma once

#include <logos/consensus/messages/common.hpp>
#include <logos/node/utility.hpp>
#include <logos/common.hpp>

#include <array>

/// TxAcceptor's transaction's message header (Message sent from TxAcceptor to the Delegate)
struct TxMessageHeader {
    const uint8_t           version = logos_version;                /// current logos software version
    const MessageType       type = MessageType::TxAcceptor_Message; /// message type
    uint16_t                mpf = 0;                                /// multipurpose field
    mutable uint32_t        payload_size = 0;                       /// size of transactions

    static constexpr uint16_t MESSAGE_SIZE = sizeof(version) + sizeof(type) + sizeof(payload_size) +
            sizeof(mpf);

    /// Class constructor
    /// @param size payload size [in]
    TxMessageHeader(uint32_t psize, uint16_t m=0) : payload_size{psize}, mpf{m} {}

    /// Class constructor
    /// @param error serialization error [in]
    /// @param stream of the serialized data [in]
    TxMessageHeader(bool &error, logos::stream &stream)
    {
        Deserialize(error, stream);
    }

    /// Class constructor
    /// @param error serialization error [in]
    /// @param buffer of the serialized data [in]
    /// @param size of the serialized data [in]
    TxMessageHeader(bool &error, const uint8_t *buf, size_t size)
    {
        Deserialize(error, buf, size);
    }

    /// Deserialize method
    /// @param error serialization error [in]
    /// @param stream of the serialized data [in]
    void Deserialize(bool &error, logos::stream &stream)
    {
        error = false;
        if (logos::read(stream, const_cast<uint8_t&>(version)) ||
            logos::read(stream, const_cast<MessageType&>(type)) ||
            logos::read(stream, const_cast<uint16_t&>(mpf)) ||
            logos::read(stream, const_cast<uint32_t&>(payload_size)))
        {
            error = true;
        }
    }

    /// Deserialize method
    /// @param error serialization error [in]
    /// @param buffer of the serialized data [in]
    /// @param size of the serialized data [in]
    void Deserialize(bool &error, const uint8_t *buf, size_t size)
    {
        logos::bufferstream stream(buf, size);
        Deserialize(error, stream);
    }

    /// Serialize method
    /// @param stream to serialize the data to [in]
    uint32_t Serialize(logos::stream &stream) const
    {
        auto s = logos::write(stream, version) +
                 logos::write(stream, type) +
                 logos::write(stream, mpf) +
                 logos::write(stream, payload_size);
        assert(s == MESSAGE_SIZE);
        return s;
    }

    /// Serialize method
    /// @param buffer to serialize the data to [in]
    uint32_t Serialize(std::vector<uint8_t> & buf) const
    {
         logos::vectorstream stream(buf);
         return Serialize(stream);
    }
};

struct TxResponse : TxMessageHeader
{
    std::vector<std::pair<logos::process_result, BlockHash>> response;
    const uint32_t RESPONSE_SIZE = sizeof(logos::process_result) + sizeof(BlockHash);

    TxResponse(const std::vector<std::pair<logos::process_result, BlockHash>> && r)
        : TxMessageHeader(RESPONSE_SIZE * r.size(), (uint16_t)r.size())
        , response(std::move(r))
    {}

    uint32_t Serialize(logos::stream &stream) const
    {
        auto s = TxMessageHeader::Serialize(stream);
        for (auto r : response)
        {
            s += logos::write(stream, r.first) + logos::write(stream, r.second);
        }
        return s;
    }

    /// Serialize method
    /// @param buffer to serialize the data to [in]
    uint32_t Serialize(std::vector<uint8_t> & buf) const
    {
        logos::vectorstream stream(buf);
        return Serialize(stream);
    }
};