#include <logos/consensus/batchblock/secondary_request_handler.hpp>

#include <logos/consensus/batchblock/batchblock_consensus_manager.hpp>

const SecondaryRequestHandler::Seconds SecondaryRequestHandler::Request::REQUEST_TIMEOUT{15};

SecondaryRequestHandler::Request::Request(Service & service,
                                          BlockPtr block,
                                          RequestManager * manager)
    : _manager(manager)
    , _block(block)
    , _timer(service, REQUEST_TIMEOUT)

{
    _timer.async_wait(std::bind(&Request::OnTimeout, this,
                                std::placeholders::_1));
}

void SecondaryRequestHandler::Request::OnTimeout(const Error & error)
{
    if(error)
    {
        if(error.value() == boost::asio::error::operation_aborted)
        {
            _manager->OnRequestDone(_block->hash());
            return;
        }

        BOOST_LOG(_log) << "SecondaryRequest::OnTimeout() - Error: "
                        << error.message();
    }

    _manager->OnRequestReady(_block->hash());
}

void SecondaryRequestHandler::Request::Cancel()
{
    _timer.cancel();
}

SecondaryRequestHandler::SecondaryRequestHandler(Service & service, RequestPromoter * promoter)
    : _service(service)
    , _promoter(promoter)
{}

bool SecondaryRequestHandler::Contains(const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return _requests.find(hash) != _requests.end();
}

void SecondaryRequestHandler::OnRequest(std::shared_ptr<logos::state_block> block)
{
    auto hash = block->hash();

    std::lock_guard<std::mutex> lock(_mutex);
    if(_requests.find(hash) != _requests.end())
    {
        BOOST_LOG(_log) << "Ignoring duplicate secondary request with hash: "
                        << hash.to_string();
        return;
    }

    _requests.emplace(std::piecewise_construct,
                      std::forward_as_tuple(hash),
                      std::forward_as_tuple(_service, block, this));
}

void SecondaryRequestHandler::OnPostCommit(const BatchStateBlock & block)
{
    for(uint64_t i = 0; i < block.block_count; ++i)
    {
        auto hash = block.blocks[i].hash();

        std::lock_guard<std::mutex> lock(_mutex);
        if(_requests.find(hash) != _requests.end())
        {
            BOOST_LOG(_log) << "SecondaryRequestHandler::OnPostCommit - "
                            << "Removing request with hash: "
                            << hash.to_string();

            _requests.erase(hash);
        }
    }

}

void SecondaryRequestHandler::OnRequestDone(const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    _requests.erase(hash);
}

void SecondaryRequestHandler::OnRequestReady(const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto entry = _requests.find(hash);

    if(entry == _requests.end())
    {
        BOOST_LOG(_log) << "SecondaryRequestHandler::OnRequestReady - Unable to find hash: "
                        << hash.to_string();
        return;
    }

    _promoter->OnRequestReady(entry->second._block);

    _requests.erase(hash);
}
