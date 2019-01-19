// @file
// TxMessage declares TxAcceptor message header
//

#pragma once

#include <logos/consensus/messages/common.hpp>

#include <array>

/// TxAcceptor's transaction's message header (Message sent from TxAcceptor to the Delegate)
struct TxMessage {
    const uint8_t           version = logos_version;                /// current logos software version
    const MessageType       type = MessageType::TxAcceptor_Message; /// message type
    uint32_t                payload_size;                           /// size of transactions

    static constexpr uint16_t MESSAGE_SIZE = sizeof(version) + sizeof(type) + sizeof(payload_size);

    /// Class constructor
    /// @param size payload size [in]
    TxMessage(uint32_t psize) : payload_size(psize) {}

    /// Class constructor
    /// @param error serialization error [in]
    /// @param stream of the serialized data [in]
    TxMessage(bool &error, logos::stream &stream)
    {
        Deserialize(error, stream);
    }

    /// Class constructor
    /// @param error serialization error [in]
    /// @param buffer of the serialized data [in]
    /// @param size of the serialized data [in]
    TxMessage(bool &error, uint8_t *buf, size_t size)
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
            logos::read(stream, const_cast<uint32_t&>(payload_size)))
        {
            error = true;
        }
    }

    /// Deserialize method
    /// @param error serialization error [in]
    /// @param buffer of the serialized data [in]
    /// @param size of the serialized data [in]
    void Deserialize(bool &error, uint8_t *buf, size_t size)
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
                 logos::write(stream, payload_size);
        assert(s == MESSAGE_SIZE);
        return s;
    }

    /// Serialize method
    /// @param buffer to serialize the data to [in]
    uint32_t Serialize(vector<uint8_t> & buf) const
    {
         logos::vectorstream stream(buf);
         return Serialize(stream);
    }
};