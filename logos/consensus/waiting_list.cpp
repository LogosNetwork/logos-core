#include <logos/consensus/waiting_list.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <functional>

template<ConsensusType CT>
const boost::posix_time::seconds WaitingList<CT>::REQUEST_TIMEOUT{5};
template<ConsensusType CT>
const  boost::posix_time::seconds WaitingList<CT>::MIN_TIMEOUT{2};

template<ConsensusType CT>
WaitingList<CT>::WaitingList(Service & service,
                                                     Promoter* promoter)
    : _service(service)
    , _promoter(promoter)
    , _timer(service)
{}

template<ConsensusType CT>
bool WaitingList<CT>::Contains(const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return _entries. template get<1>().find(hash) != _entries. template get<1>().end();
}

template<ConsensusType CT>
void WaitingList<CT>::OnMessage(std::shared_ptr<DelegateMessage<CT>> block, Seconds seconds)
{
    auto hash = block->Hash();

    std::lock_guard<std::mutex> lock(_mutex);
    if(_entries. template get<1>().find(hash) != _entries. template get<1>().end())
    {
        LOG_WARN(_log) << "Ignoring duplicate secondary request with hash: "
                       << hash.to_string();
        return;
    }

    _entries.insert(Entry{hash, block, Clock::universal_time() + seconds});

    if(_entries.size() == 1)
    {
        ScheduleTimer(seconds);
    }
}

template<ConsensusType CT>
void WaitingList<CT>::OnTimeout(const Error & error)
{
    std::vector<Entry> ready_requests;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(error)
        {
            if (error == boost::asio::error::operation_aborted)
            {
                return;
            }
            LOG_INFO(_log) << "WaitingList<" << ConsensusToName(CT)
                           << ">::OnTimeout - Error: "
                           << error.message();
        }

        auto now = Clock::universal_time();
        auto entry = _entries. template get<0>().begin();

        for(; entry != _entries. template get<0>().end() && entry->expiration <= now;
            ++entry)
        {
            ready_requests.push_back(*entry);
        }

        _entries. template get<0>().erase(_entries. template get<0>().begin(), entry);

        if(!_entries.empty())
        {
            auto timeout = std::max(MIN_TIMEOUT.total_seconds(),
                                    (_entries. template get<0>().begin()->expiration
                                     - now).total_seconds());

            ScheduleTimer(Seconds(timeout));
        }
    }

    std::lock_guard<std::mutex> lock(_promoter_mutex);
    if (_promoter == nullptr)
    {
        LOG_ERROR(_log) << "WaitingList::OnTimeout promoter is nullptr";
        return;
    }

    for(auto & request : ready_requests)
    {
        _promoter->OnMessageReady(request.block);
    }
}

template<ConsensusType CT>
void WaitingList<CT>::OnPostCommit(const PrePrepare & message)
{
    std::lock_guard<std::mutex> lock(_mutex);

    PruneMessages(message);

    if (_entries.empty())
    {
        _timer.cancel();
    }
}

template<ConsensusType CT>
void WaitingList<CT>::ScheduleTimer(const Seconds & timeout)
{
    _timer.expires_from_now(timeout);
    _timer.async_wait(std::bind(&WaitingList::OnTimeout, this,
                                std::placeholders::_1));
}

template <ConsensusType CT>
void WaitingList<CT>::PruneMessage(const BlockHash &hash)
{
    if(_entries. template get<1>().find(hash) != _entries. template get<1>().end())
    {
        LOG_INFO(_log) << "WaitingList<" << ConsensusToName(CT)
                       << ">::PruneRequests - "
                       << "Removing request with hash: "
                       << hash.to_string();

        _entries. template get<1>().erase(hash);
    }
}

template <ConsensusType CT>
void WaitingList<CT>::UpdateMessagePromoter(MessagePromoter<CT>* promoter)
{
    std::lock_guard<std::mutex> lock(_promoter_mutex);
    _promoter = promoter;
}

template<>
void WaitingList<ConsensusType::MicroBlock>::PruneMessages(const PrePrepare & block)
{
    PruneMessage(block.Hash());
}

template<>
void WaitingList<ConsensusType::Epoch>::PruneMessages(const PrePrepare & block)
{
    PruneMessage(block.Hash());
}

template<>
void WaitingList<ConsensusType::Request>::PruneMessages(const PrePrepare & block)
{
    for (uint64_t i = 0; i < block.requests.size(); ++i)
    {
        BlockHash hash = block.requests[i]->GetHash();

        PruneMessage(hash);
    }
}

template class WaitingList<ConsensusType::Request>;
template class WaitingList<ConsensusType::MicroBlock>;
template class WaitingList<ConsensusType::Epoch>;
