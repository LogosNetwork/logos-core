#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/util.hpp>
#include <logos/consensus/request/request_internal_queue.hpp>
#include <logos/lib/epoch_time_util.hpp>
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

/// Main handler for consensus messages
///
/// This class serves as central queue for consensus messages from
/// 1) incoming requests / archive blocks,
/// 2) secondary waiting list, and
/// 3) backups
template<ConsensusType CT>
class MessageHandler
{
protected:
    using Seconds    = boost::posix_time::seconds;

public:

    using PrePrepare = PrePrepareMessage<CT>;
    using MessagePtr = std::shared_ptr<DelegateMessage<CT>>;

    MessageHandler();

    /// Queues smart pointer to incoming message
    ///
    /// @param[in] message to queue
    /// @param[in] seconds from now at which this message is ready to be included in primary consensus
    void OnMessage(const MessagePtr & message, const Seconds & seconds = Seconds{0});

    /// Queues smart pointer to incoming message
    ///
    /// @param[in] message to queue
    /// @param[in] absolute timepoint at which this message is ready to be included in primary consensus
    void OnMessage(const MessagePtr & message, const TimePoint &);

    /// Peaks into the front of the sequenced queue
    ///
    /// @return shared pointer to the queue's first message, if one exists, otherwise nullptr
    MessagePtr GetFront();

    /// Attempts to erase post-committed message from queue
    ///
    /// @param[in] shared pointer to PrePrepare message to erase
    template<ConsensusType PCT = CT>
    std::enable_if_t<PCT != ConsensusType::Request, void> OnPostCommit(std::shared_ptr<PrePrepare> block)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto & hashed = _entries. template get <1> ();
        auto hash = block->Hash();
        auto n_erased = hashed.erase(hash);
        if (n_erased)
        {
            LOG_DEBUG (_log) << "MessageHandler<" << ConsensusToName(CT) << ">::OnPostCommit - erased " << hash.to_string();
        }
        else
        {
            LOG_WARN (_log) << "MessageHandler<" << ConsensusToName(CT) << ">::OnPostCommit - hash does not exist: "
                            << hash.to_string();
            // For MB and EB, we also need to erase based on <epoch, sequence> slot
            // (until better rejection logic handling is implemented)
            for (auto it = _entries.begin(); it != _entries.end(); it++)
            {
                if (it->block->epoch_number == block->epoch_number && it->block->sequence == block->sequence)
                {
                    LOG_ERROR(_log) << "MessageHandler<" << ConsensusToName(CT)
                                    << ">::OnPostCommit - queued conflicting archival block detected: "
                                    << it->block->ToJson();
                    _entries.erase(it);
                    break;
                }
            }
        }
    }

    /// Attempts to erase all contents from post-committed message from queue
    ///
    /// @param[in] shared pointer to PrePrepare whose requests we wish to erase
    void OnPostCommit(std::shared_ptr<PrePrepareMessage<ConsensusType::Request>>);

    /// Checks if no message in queue is ready for primary consensus
    ///
    /// @return true if nothing in queue has a timestamp lower than current time
    bool PrimaryEmpty();

    /// Gets the most imminent timeout value in the future
    ///
    /// @return most imminent timeout value in the future, if one exists, otherwise the lowest timestamp value possible
    /// (defined in boost posix_time)
    const TimePoint & GetImminentTimeout();

    /// Checks if queue contains the given hash value
    ///
    /// @param[in] hash to check
    /// @return true if exists
    bool Contains(const BlockHash & hash);

    /// Clears everything in queue if delegate node is about to retire
    void Clear();

    /// Below are benchmarking methods, deprecated for now
    bool BatchFull();
    bool Empty();
    /// above is deprecated

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

    /// Moves queued requests to RequestConsensusManager's internal queue, up to the specified size
    ///
    /// @param[in] reference to target RequestManager internal queue
    /// @param[in] max size to copy
    void MoveToTarget(RequestInternalQueue &, size_t);

    /// Instantiates a static singleton of RequestMessageHandler, if one doesn't exist, and return its reference
    static RequestMessageHandler & GetMessageHandler()
    {
        static RequestMessageHandler handler;
        return handler;
    }
};

class MicroBlockMessageHandler : public MessageHandler<ConsensusType::MicroBlock>
{
public:
    /// Fetches the most recently queued message's sequence and epoch numbers
    /// This function is called by Archiver to ascertain the latest MB epoch + sequence numbers in queue, if any exists
    ///
    /// @param[in] reference to sequence number to write to, set to 0 if queue is empty
    /// @param[in] reference to epoch number to write to, set to 0 if queue is empty
    /// @return true if queue contains content, false if queue is empty
    bool GetQueuedSequence(EpochSeq &);

    /// Instantiates a static singleton of MicroBlockMessageHandler, if one doesn't exist, and return its reference
    static MicroBlockMessageHandler & GetMessageHandler()
    {
        static MicroBlockMessageHandler handler;
        return handler;
    }
};

class EpochMessageHandler : public MessageHandler<ConsensusType::Epoch>
{
public:

    /// Instantiates a static singleton of EpochMessageHandler, if one doesn't exist, and return its reference
    static EpochMessageHandler & GetMessageHandler()
    {
        static EpochMessageHandler handler;
        return handler;
    }
};
