// @file
// Declares sink for consensus message
//

#pragma once

#include <logos/consensus/consensus_msg_consumer.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/log.hpp>

#include <boost/asio/io_service.hpp>

#include <queue>
#include <mutex>

/// Sink for consensus messages, is also consumer of the message queue
class ConsensusMsgSink : public ConsensusMsgConsumer
{

    /// Message stored on the queue
    struct Message {
        bool                            is_p2p;         /// Is the message received via p2p
        MessageType                     message_type;   /// Consensus message type
        std::shared_ptr<MessageBase>    message;        /// Message pointer
    };

protected:
    using Service   = boost::asio::io_service;

public:
    /// Class constructor
    /// @param service boost asio service
    ConsensusMsgSink(Service &service);
    /// Class destructor
    virtual ~ConsensusMsgSink() = default;
    /// Push the message onto the queue
    /// @param data buffer
    /// @param version logos protocol version
    /// @param message_type consensus message type
    /// @param consensus_type consensus type
    /// @param payload_size buffer size
    /// @param is_p2p true if message is received via p2p
    /// @returns true on success
    bool Push(const uint8_t * data, uint8_t version, MessageType message_type,
              ConsensusType consensus_type, uint32_t payload_size, bool is_p2p);
    /// Pops message from the queue and post for consuming to the thread pool
    void Pop();
    /// Post the message for consuming on the thread pool
    /// @param message pointer
    /// @param message_type consensus message type
    /// @param is_p2p true if received via p2p
    void Post(std::shared_ptr<MessageBase> message, MessageType message_type, bool is_p2p);
    /// Reset connection statistics
    void ResetConnectCount()
    {
        _direct_connect = 0;
        _p2p_connect = 0;
    }
    /// @returns true if messages designated to the primary delegate are received via
    ///   direct connection
    bool PrimaryDirectlyConnected()
    {
        // either received direct messages or have not received any messages
        return _direct_connect > 0 || _p2p_connect == 0;
    }

protected:
    Service &               _service;           /// Boost asio service
    std::queue<Message>     _msg_queue;         /// Message queue of consensus messages
    std::mutex              _queue_mutex;       /// Queue mutex
    std::atomic<uint32_t>   _direct_connect;    /// Direct connections count
    std::atomic<uint32_t>   _p2p_connect;       /// P2p connections count
    bool                    _consuming;         /// Is message currently being consumed
    Log                     _log;               /// Log object
};

