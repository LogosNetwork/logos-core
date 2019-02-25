//
// Created by gregt on 2/13/19.
//

#include <logos/node/delegate_identity_manager.hpp>
#include <logos/consensus/consensus_msg_sink.hpp>

#include <sys/stat.h>

#define P2PTEST

ConsensusMsgSink::ConsensusMsgSink(Service &service)
    : _service(service)
{}

bool
ConsensusMsgSink::Push(uint8_t delegate_id,
                       const uint8_t * data,
                       uint8_t version,
                       MessageType message_type,
                       ConsensusType consensus_type,
                       uint32_t payload_size,
                       bool is_p2p)
{
    std::lock_guard<std::mutex> lock(_queue_mutex);

#ifdef P2PTEST
    struct stat sb;
    std::string path = "./DB/Consensus_" +
                       std::to_string((int)DelegateIdentityManager::_global_delegate_idx) +
                       "/p2ptest";
    if (stat(path.c_str(), &sb) == 0 && (sb.st_mode & S_IFMT) == S_IFREG)
    {
        return true;
    }
#endif

    _direct_connect += ((false == is_p2p && message_type != MessageType::Pre_Prepare &&
            message_type != MessageType::Post_Prepare && message_type != MessageType::Post_Commit) ? 1 : 0);

    auto message = Parse(data, version, message_type, consensus_type, payload_size);
    if (message == nullptr)
    {
        return false;
    }

    if (false == _consuming)
    {
        _consuming = true;
        if (!_msg_queue.empty())
        {
            _msg_queue.emplace(Message{is_p2p, message_type, message});

            auto toconsume = _msg_queue.front();
            _msg_queue.pop();

            message = toconsume.message;
            message_type = toconsume.message_type;
            is_p2p = toconsume.is_p2p;
        }

        Post(message, message_type, is_p2p);
    }
    else
    {
        _msg_queue.emplace(Message{is_p2p, message_type, message});
    }
    return true;
}

void
ConsensusMsgSink::Pop()
{
    std::lock_guard<std::mutex> lock(_queue_mutex);

    if (_msg_queue.empty())
    {
        _consuming = false;
        return;
    }

    auto toconsume = _msg_queue.front();
    _msg_queue.pop();

    Post(toconsume.message, toconsume.message_type, toconsume.is_p2p);
}

void
ConsensusMsgSink::Post(std::shared_ptr<MessageBase> message, MessageType message_type, bool is_p2p)
{
    _service.post([this, message, message_type, is_p2p]() {
        OnMessage(message, message_type, is_p2p);
        Pop();
    });
}
