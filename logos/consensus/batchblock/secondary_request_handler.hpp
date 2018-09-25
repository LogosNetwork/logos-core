#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>

#include <unordered_map>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio/io_service.hpp>

class RequestPromoter;

class RequestManager
{
public:

    virtual void OnRequestDone(const logos::block_hash & hash) = 0;
    virtual void OnRequestReady(const logos::block_hash & hash) = 0;

    virtual ~RequestManager() {}
};

class SecondaryRequestHandler : public RequestManager
{
    class Request;

    using Service  = boost::asio::io_service;
    using Error    = boost::system::error_code;
    using Log      = boost::log::sources::logger_mt;
    using BlockPtr = std::shared_ptr<logos::state_block>;
    using Seconds  = boost::posix_time::seconds;
    using Requests = std::unordered_map<logos::block_hash,
                                        Request>;

    struct Request
    {
        using Timer = boost::asio::deadline_timer;

        Request(Service & service,
                BlockPtr block,
                RequestManager * manager);

        void OnTimeout(const Error & error);
        void Cancel();

        static const Seconds REQUEST_TIMEOUT;

        RequestManager * _manager;
        BlockPtr         _block;
        Timer            _timer;
        Log              _log;
    };

public:

    SecondaryRequestHandler(Service & service, RequestPromoter * promoter);

    bool Contains(const logos::block_hash & hash);

    void OnRequest(std::shared_ptr<logos::state_block> block);

    void OnPostCommit(const BatchStateBlock & block);

    void OnRequestDone(const logos::block_hash & hash) override;
    void OnRequestReady(const logos::block_hash & hash) override;

private:

    Requests          _requests;
    Service &         _service;
    RequestPromoter * _promoter;
    Log               _log;
    std::mutex        _mutex;
};
