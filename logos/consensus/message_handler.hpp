#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/consensus/request/request_internal_queue.hpp>
#include <logos/lib/epoch_time_util.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/log.hpp>

#include <memory>
#include <list>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index_container.hpp>

using boost::multi_index::hashed_unique;
using boost::multi_index::indexed_by;
using boost::multi_index::member;
using boost::multi_index::ordered_non_unique;
using boost::multi_index::sequenced;

class RequestInternalQueue;

template<ConsensusType CT>
class MessageHandler
{
protected:
    using Seconds    = boost::posix_time::seconds;

public:

    using PrePrepare = PrePrepareMessage<CT>;
    using MessagePtr = std::shared_ptr<DelegateMessage<CT>>;

    MessageHandler();

    void OnMessage(const MessagePtr & message, const Seconds & seconds = Seconds{0});
    MessagePtr GetFront();
    virtual void OnPostCommit(std::shared_ptr<PrePrepare>);

    bool BatchFull();
    bool Empty();
    bool PrimaryEmpty();
    const TimePoint & GetImminentTimeout();

    bool Contains(const BlockHash & hash);
    void Clear();

private:

    struct Entry
    {
        BlockHash  hash;
        MessagePtr block;
        TimePoint  expiration;
    };

    using Entries   =
            boost::multi_index_container<
                Entry,
                indexed_by<
                    sequenced<>,
                    hashed_unique<member<Entry, BlockHash, &Entry::hash>>,
                    ordered_non_unique<member<Entry, TimePoint, &Entry::expiration>>
                >
            >;

protected:

    std::mutex _mutex;
    Log        _log;
    Entries    _entries;
};

class RequestMessageHandler : public MessageHandler<ConsensusType::Request>
{
public:

    void OnPostCommit(std::shared_ptr<PrePrepareMessage<ConsensusType::Request>>) override;
    void MoveToTarget(RequestInternalQueue &, size_t);
    static RequestMessageHandler & GetMessageHandler()
    {
        static RequestMessageHandler handler;
        return handler;
    }
};

class MicroBlockMessageHandler : public MessageHandler<ConsensusType::MicroBlock>
{
public:
    void GetQueuedSequence(uint32_t &, uint32_t &);
    static MicroBlockMessageHandler & GetMessageHandler()
    {
        static MicroBlockMessageHandler handler;
        return handler;
    }
};

class EpochMessageHandler : public MessageHandler<ConsensusType::Epoch>
{
public:

    static EpochMessageHandler & GetMessageHandler()
    {
        static EpochMessageHandler handler;
        return handler;
    }
};
