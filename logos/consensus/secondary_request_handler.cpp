#include <logos/consensus/secondary_request_handler.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <functional>

template<ConsensusType CT>
const boost::posix_time::seconds SecondaryRequestHandler<CT>::REQUEST_TIMEOUT{5};
template<ConsensusType CT>
const  boost::posix_time::seconds SecondaryRequestHandler<CT>::MIN_TIMEOUT{2};

template<ConsensusType CT>
SecondaryRequestHandler<CT>::SecondaryRequestHandler(Service & service, RequestPromoter<CT> & promoter)
    : _service(service)
    , _promoter(promoter)
    , _timer(service)
{}

template<ConsensusType CT>
bool SecondaryRequestHandler<CT>::Contains(const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return _requests.get<1>().find(hash) != _requests.get<1>().end();
}

template<ConsensusType CT>
void SecondaryRequestHandler<CT>::OnRequest(std::shared_ptr<RequestMessage<CT>> block, Seconds seconds)
{
    auto hash = block->hash();

    std::lock_guard<std::mutex> lock(_mutex);
    if(_requests.get<1>().find(hash) != _requests.get<1>().end())
    {
        LOG_WARN(_log) << "Ignoring duplicate secondary request with hash: "
                       << hash.to_string();
        return;
    }

    _requests.insert(Request{hash, block, Clock::universal_time() + seconds});

    if(_requests.size() == 1)
    {
        ScheduleTimer(REQUEST_TIMEOUT);
    }
}

template<ConsensusType CT>
void SecondaryRequestHandler<CT>::OnTimeout(const Error & error)
{
    std::vector<Request> ready_requests;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(error)
        {
            LOG_INFO(_log) << "SecondaryRequestHandler<" << ConsensusToName(CT)
                           << ">::OnTimeout - Error: "
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
            auto timeout = std::max(MIN_TIMEOUT.total_seconds(),
                                    (_requests.get<0>().begin()->expiration
                                     - now).total_seconds());

            ScheduleTimer(Seconds(timeout));
        }
    }

    for(auto & request : ready_requests)
    {
        _promoter.OnRequestReady(request.block);
    }
}

template<ConsensusType CT>
void SecondaryRequestHandler<CT>::OnPostCommit(const PrePrepare & message)
{
    PruneRequests(message);
}

template<ConsensusType CT>
void SecondaryRequestHandler<CT>::ScheduleTimer(const Seconds & timeout)
{
    _timer.expires_from_now(timeout);
    _timer.async_wait(std::bind(&SecondaryRequestHandler::OnTimeout, this,
                                std::placeholders::_1));
}

template <ConsensusType CT>
void SecondaryRequestHandler<CT>::PruneRequest(const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(_requests.get<1>().find(hash) != _requests.get<1>().end())
    {
        LOG_INFO(_log) << "SecondaryRequestHandler<" << ConsensusToName(CT)
                       << ">::PruneRequests - "
                       << "Removing request with hash: "
                       << hash.to_string();

        _requests.get<1>().erase(hash);
    }
}

template<>
void SecondaryRequestHandler<ConsensusType::MicroBlock>::PruneRequests(const PrePrepare & block)
{
    PruneRequest(block.hash());
}

template<>
void SecondaryRequestHandler<ConsensusType::Epoch>::PruneRequests(const PrePrepare & block)
{
    PruneRequest(block.hash());
}

template<>
void SecondaryRequestHandler<ConsensusType::BatchStateBlock>::PruneRequests(const PrePrepare & block)
{
    for (uint64_t i = 0; i < block.block_count; ++i)
    {
        auto hash = block.blocks[i].hash();

        PruneRequest(hash);
    }
}

template class SecondaryRequestHandler<ConsensusType::BatchStateBlock>;
template class SecondaryRequestHandler<ConsensusType::MicroBlock>;
template class SecondaryRequestHandler<ConsensusType::Epoch>;
