#include <logos/consensus/batchblock/secondary_request_handler.hpp>

#include <logos/consensus/batchblock/batchblock_consensus_manager.hpp>

const SecondaryRequestHandler::Seconds SecondaryRequestHandler::REQUEST_TIMEOUT{5};
const SecondaryRequestHandler::Seconds SecondaryRequestHandler::MIN_TIMEOUT{2};

SecondaryRequestHandler::SecondaryRequestHandler(Service & service, RequestPromoter * promoter)
    : _service(service)
    , _promoter(promoter)
    , _timer(service, REQUEST_TIMEOUT)
{}

bool SecondaryRequestHandler::Contains(const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return _requests.get<1>().find(hash) != _requests.get<1>().end();
}

void SecondaryRequestHandler::OnRequest(std::shared_ptr<logos::state_block> block)
{
    auto hash = block->hash();

    std::lock_guard<std::mutex> lock(_mutex);
    if(_requests.get<1>().find(hash) != _requests.get<1>().end())
    {
        BOOST_LOG(_log) << "Ignoring duplicate secondary request with hash: "
                        << hash.to_string();
        return;
    }

    _requests.insert(Request{hash, block, Clock::universal_time() + REQUEST_TIMEOUT});

    if(_requests.size() == 1)
    {
        ScheduleTimer(REQUEST_TIMEOUT);
    }
}

void SecondaryRequestHandler::OnTimeout(const Error & error)
{
    std::vector<Request> ready_requests;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(error)
        {
            BOOST_LOG(_log) << "SecondaryRequestHandler::OnTimeout - Error: "
                            << error.message();
        }

        auto now = Clock::universal_time();
        auto entry = _requests.get<0>().begin();

        for(; entry != _requests.get<0>().end() && entry->expiration <= now;
            ++entry)
        {
            ready_requests.push_back(*entry);
        }

        _requests.get<0>().erase(_requests.get<0>().begin(), entry);

        if(!_requests.empty())
        {
            auto timeout = std::max(MIN_TIMEOUT.ticks(),
                                    (_requests.get<0>().begin()->expiration
                                     - now).seconds());

            ScheduleTimer(Seconds(timeout));
        }
    }

    for(auto & request : ready_requests)
    {
        _promoter->OnRequestReady(request.block);
    }
}

void SecondaryRequestHandler::OnPrePrepare(const BatchStateBlock & block)
{
    PruneRequests(block);
}

void SecondaryRequestHandler::ScheduleTimer(const Seconds & timeout)
{
    _timer.expires_from_now(timeout);
    _timer.async_wait(std::bind(&SecondaryRequestHandler::OnTimeout, this,
                                std::placeholders::_1));
}

void SecondaryRequestHandler::PruneRequests(const BatchStateBlock & block)
{
    for(uint64_t i = 0; i < block.block_count; ++i)
    {
        auto hash = block.blocks[i].hash();

        std::lock_guard<std::mutex> lock(_mutex);
        if(_requests.get<1>().find(hash) != _requests.get<1>().end())
        {
            BOOST_LOG(_log) << "SecondaryRequestHandler::PruneRequests - "
                            << "Removing request with hash: "
                            << hash.to_string();

            _requests.get<1>().erase(hash);
        }
    }
}
