#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/common.hpp>

#include <unordered_map>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>

class RequestPromoter;

using boost::multi_index::ordered_non_unique;
using boost::multi_index::hashed_unique;
using boost::multi_index::indexed_by;
using boost::multi_index::member;

class SecondaryRequestHandler
{

    class Request;

    using Timer     = boost::asio::deadline_timer;
    using Service   = boost::asio::io_service;
    using Error     = boost::system::error_code;
    using Log       = boost::log::sources::logger_mt;
    using BlockPtr  = std::shared_ptr<logos::state_block>;
    using Seconds   = boost::posix_time::seconds;
    using Clock     = boost::posix_time::second_clock;
    using TimePoint = boost::posix_time::ptime;

    struct Request
    {
        logos::block_hash hash;
        BlockPtr          block;
        TimePoint         expiration;
    };

    using Requests =
            boost::multi_index_container<
                Request,
                indexed_by<
                    ordered_non_unique<member<Request, TimePoint, &Request::expiration>>,
                    hashed_unique<member<Request, logos::block_hash, &Request::hash>>
                >
            >;

public:

    SecondaryRequestHandler(Service & service, RequestPromoter * promoter);

    bool Contains(const logos::block_hash & hash);

    void OnRequest(std::shared_ptr<logos::state_block> block);
    void OnTimeout(const Error & error);

    void OnPrePrepare(const BatchStateBlock & block);

private:

    void ScheduleTimer(const Seconds & timeout);
    void PruneRequests(const BatchStateBlock & block);

    static const Seconds REQUEST_TIMEOUT;
    static const Seconds MIN_TIMEOUT;

    Requests          _requests;
    Service &         _service;
    RequestPromoter * _promoter;
    Log               _log;
    std::mutex        _mutex;
    Timer             _timer;
};
