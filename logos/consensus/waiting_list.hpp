#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <unordered_map>

#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>

template<ConsensusType CT>
class MessagePromoter;

using boost::multi_index::ordered_non_unique;
using boost::multi_index::hashed_unique;
using boost::multi_index::indexed_by;
using boost::multi_index::member;

template<ConsensusType CT>
class WaitingList
{
    using Timer      = boost::asio::deadline_timer;
    using Service    = boost::asio::io_service;
    using Error      = boost::system::error_code;
    using MessagePtr = std::shared_ptr<DelegateMessage<CT>>;
    using Seconds    = boost::posix_time::seconds;
    using Clock      = boost::posix_time::second_clock;
    using TimePoint  = boost::posix_time::ptime;
    using PrePrepare = PrePrepareMessage<CT>;
    using Promoter   = MessagePromoter<CT>;

    struct Entry
    {
        BlockHash  hash;
        MessagePtr block;
        TimePoint  expiration;
    };

    using Entries =
            boost::multi_index_container<
                Entry,
                indexed_by<
                    ordered_non_unique<member<Entry, TimePoint, &Entry::expiration>>,
                    hashed_unique<member<Entry, BlockHash, &Entry::hash>>
                >
            >;

public:

    WaitingList(Service & service, Promoter * promoter);

    bool Contains(const BlockHash & hash);

    void OnMessage(std::shared_ptr<DelegateMessage<CT>> block,
                   Seconds seconds = REQUEST_TIMEOUT);

    void OnTimeout(const Error & error);

    void OnPostCommit(const PrePrepare & message);

    void UpdateMessagePromoter(MessagePromoter<CT>* promoter);

    void  ClearWaitingList()
    {
        UpdateMessagePromoter(nullptr);
        std::lock_guard<std::mutex> l(_mutex);
        _entries. template get<0>().erase(_entries. template get<0>().begin(), _entries. template get<0>().end());
        _timer.cancel();
    }

private:

    void PruneMessage(const BlockHash &hash);

    void ScheduleTimer(const Seconds & timeout);
    void PruneMessages(const PrePrepare & block);

    static const Seconds REQUEST_TIMEOUT;
    static const Seconds MIN_TIMEOUT;

    Entries    _entries;
    Service &  _service;
    Promoter * _promoter;
    Log        _log;
    std::mutex _mutex;
    Timer      _timer;
    std::mutex _promoter_mutex;
};
