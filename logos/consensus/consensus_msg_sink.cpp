// @file
// Defines sink for consensus message
//

#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/consensus/consensus_msg_sink.hpp>
#include <logos/consensus/messages/util.hpp>

#include <logos/consensus/backup_delegate.hpp>

#include <sys/stat.h>

ConsensusMsgSink::ConsensusMsgSink(Service &service)
    : _service(service)
{}

bool
ConsensusMsgSink::Push(const uint8_t * data,
                       uint8_t version,
                       MessageType message_type,
                       ConsensusType consensus_type,
                       uint32_t payload_size,
                       bool is_p2p)
{
    std::lock_guard<std::mutex> lock(_queue_mutex);

    bool dest_primary = message_type == MessageType::Prepare ||
            message_type == MessageType::Rejection || message_type == MessageType::Commit;
    _direct_connect += ((false == is_p2p && dest_primary) ? 1 : 0);

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
            _msg_queue.emplace(Message{is_p2p, message_type, consensus_type, message});

            auto toconsume = _msg_queue.front();
            _msg_queue.pop();

            message = toconsume.message;
            message_type = toconsume.message_type;
            is_p2p = toconsume.is_p2p;
        }

        Post(message, message_type, consensus_type, is_p2p);
    }
    else
    {
        _msg_queue.emplace(Message{is_p2p, message_type, consensus_type, message});
    }
    return true;
}

template <ConsensusType CT>
bool ConsensusMsgSink::Push(PostCommittedBlock<CT> const & block)
{
    auto message_type = MessageType::Post_Committed_Block;
    auto consensus_type = CT;
    auto is_p2p = true;
    std::shared_ptr<MessageBase> message = std::make_shared<PostCommittedBlock<CT>>(block);
    std::lock_guard<std::mutex> lock(_queue_mutex);
    if (false == _consuming)
    {
        _consuming = true;
        if (!_msg_queue.empty())
        {
            _msg_queue.emplace(Message{is_p2p,message_type,consensus_type, message});

            auto toconsume = _msg_queue.front();
            _msg_queue.pop();

            message = toconsume.message;
            message_type = toconsume.message_type;
            is_p2p = toconsume.is_p2p;
        }

        Post(message, message_type, consensus_type, is_p2p);
    }
    else
    {
        _msg_queue.emplace(Message{is_p2p, message_type, consensus_type, message});
    }
    return true;
}

template
bool ConsensusMsgSink::Push(PostCommittedBlock<ConsensusType::Request> const & block);

template
bool ConsensusMsgSink::Push(PostCommittedBlock<ConsensusType::MicroBlock> const & block);

template
bool ConsensusMsgSink::Push(PostCommittedBlock<ConsensusType::Epoch> const & block);

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

    Post(toconsume.message, toconsume.message_type, toconsume.consensus_type, toconsume.is_p2p);
}

void
ConsensusMsgSink::Post(std::shared_ptr<MessageBase> message,
                       MessageType message_type,
                       ConsensusType consensus_type,
                       bool is_p2p)
{
    std::weak_ptr<ConsensusMsgSink> this_w = shared_from_this();
    _service.post([this_w, message, message_type, consensus_type, is_p2p]() {
        auto this_s = GetSharedPtr(this_w, "ConsensusMsgSink::Post, object destroyed");
        if (!this_s)
        {
            return;
        }
        this_s->OnMessage(message, message_type, consensus_type, is_p2p);
        this_s->Pop();
    });
}
