// @file
// Declares sink for consensus message
//

#pragma once

#include <logos/consensus/consensus_msg_consumer.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <boost/asio/io_service.hpp>

#include <queue>
#include <mutex>

class ConsensusMsgSink : public ConsensusMsgConsumer
{

    struct Message {
        bool                            is_p2p;
        MessageType                     message_type;
        std::shared_ptr<MessageBase>    message;
    };
    struct ConnCnts {
        uint32_t    dir_cnt;
        uint32_t    p2p_cnt;
    };

protected:
    using Service   = boost::asio::io_service;

public:
    ConsensusMsgSink(Service &service);
    virtual ~ConsensusMsgSink() = default;
    bool Push(uint8_t delegate_id, const uint8_t * data, uint8_t version,
              MessageType message_type, ConsensusType consensus_type, uint32_t payload_size, bool is_p2p);
    void Pop();
    void Post(std::shared_ptr<MessageBase> message, MessageType message_type, bool is_p2p);
    void ResetConnectStats()
    {
        _direct_connect = 0;
    }
    bool IsDirectPrimary()
    {
        return _direct_connect > 0;
    }


protected:
    Service &               _service;
    std::queue<Message>     _msg_queue;
    std::mutex              _queue_mutex;
    std::atomic<uint32_t>   _direct_connect;
    bool                    _consuming;
};

