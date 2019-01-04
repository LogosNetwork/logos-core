#include <logos/consensus/secondary_request_handler.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <functional>

template<ConsensusType CT>
const boost::posix_time::seconds SecondaryRequestHandler<CT>::REQUEST_TIMEOUT{5000000};
template<ConsensusType CT>
const  boost::posix_time::seconds SecondaryRequestHandler<CT>::MIN_TIMEOUT{2000000};

template<ConsensusType CT>
SecondaryRequestHandler<CT>::SecondaryRequestHandler(Service & service,
                                                     Promoter* promoter)
    : _service(service)
    , _promoter(promoter)
    , _timer(service)
{}

template<ConsensusType CT>
bool SecondaryRequestHandler<CT>::Contains(const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return _requests. template get<1>().find(hash) != _requests. template get<1>().end();
}

template<ConsensusType CT>
void SecondaryRequestHandler<CT>::OnRequest(std::shared_ptr<RequestMessage<CT>> block, Seconds seconds)
{
    auto hash = block->Hash();

    std::lock_guard<std::mutex> lock(_mutex);
    if(_requests. template get<1>().find(hash) != _requests. template get<1>().end())
    {
        LOG_WARN(_log) << "Ignoring duplicate secondary request with hash: "
                       << hash.to_string();
        return;
    }

    _requests.insert(Request{hash, block, Clock::universal_time() + seconds});

    if(_requests.size() == 1)
    {
        ScheduleTimer(seconds);
    }
}

template<ConsensusType CT>
void SecondaryRequestHandler<CT>::OnTimeout(const Error & error)
{
//    std::vector<Request> ready_requests;
//
//    {
//        std::lock_guard<std::mutex> lock(_mutex);
//
//        if(error)
//        {
//            if (error == boost::asio::error::operation_aborted)
//            {
//                return;
//            }
//            LOG_INFO(_log) << "SecondaryRequestHandler<" << ConsensusToName(CT)
//                           << ">::OnTimeout - Error: "
//                           << error.message();
//        }
//
//        auto now = Clock::universal_time();
//        auto entry = _requests. template get<0>().begin();
//
//        for(; entry != _requests. template get<0>().end() && entry->expiration <= now;
//            ++entry)
//        {
//            ready_requests.push_back(*entry);
//        }
//
//        _requests. template get<0>().erase(_requests. template get<0>().begin(), entry);
//
//        if(!_requests.empty())
//        {
//            auto timeout = std::max(MIN_TIMEOUT.total_seconds(),
//                                    (_requests. template get<0>().begin()->expiration
//                                     - now).total_seconds());
//
//            ScheduleTimer(Seconds(timeout));
//        }
//    }
//
//    std::lock_guard<std::mutex> lock(_promoter_mutex);
//    if (_promoter == nullptr)
//    {
//        LOG_ERROR(_log) << "SecondaryRequestHandler::OnTimeout promoter is nullptr";
//        return;
//    }
//
//    for(auto & request : ready_requests)
//    {
//        _promoter->OnRequestReady(request.block);
//    }
}

template<ConsensusType CT>
void SecondaryRequestHandler<CT>::OnPostCommit(const PrePrepare & message)
{
    std::lock_guard<std::mutex> lock(_mutex);

    PruneRequests(message);

    if (_requests.empty())
    {
        _timer.cancel();
    }
}

template<ConsensusType CT>
void SecondaryRequestHandler<CT>::ScheduleTimer(const Seconds & timeout)
{
    _timer.expires_from_now(timeout);
    _timer.async_wait(std::bind(&SecondaryRequestHandler::OnTimeout, this,
                                std::placeholders::_1));
}

template <ConsensusType CT>
void SecondaryRequestHandler<CT>::PruneRequest(const BlockHash & hash)
{
    if(_requests. template get<1>().find(hash) != _requests. template get<1>().end())
    {
        LOG_INFO(_log) << "SecondaryRequestHandler<" << ConsensusToName(CT)
                       << ">::PruneRequests - "
                       << "Removing request with hash: "
                       << hash.to_string();

        _requests. template get<1>().erase(hash);
    }
}

template <ConsensusType CT>
void SecondaryRequestHandler<CT>::UpdateRequestPromoter(RequestPromoter<CT>* promoter)
{
    std::lock_guard<std::mutex> lock(_promoter_mutex);
    _promoter = promoter;
}

template<>
void SecondaryRequestHandler<ConsensusType::MicroBlock>::PruneRequests(const PrePrepare & block)
{
    PruneRequest(block.Hash());
}

template<>
void SecondaryRequestHandler<ConsensusType::Epoch>::PruneRequests(const PrePrepare & block)
{
    PruneRequest(block.Hash());
}

template<>
void SecondaryRequestHandler<ConsensusType::BatchStateBlock>::PruneRequests(const PrePrepare & block)
{
    for (uint64_t i = 0; i < block.block_count; ++i)
    {
        BlockHash hash = block.blocks[i].GetHash();

        PruneRequest(hash);
    }
}

template class SecondaryRequestHandler<ConsensusType::BatchStateBlock>;
template class SecondaryRequestHandler<ConsensusType::MicroBlock>;
template class SecondaryRequestHandler<ConsensusType::Epoch>;
