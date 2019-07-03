#include <logos/consensus/message_handler.hpp>
#include <logos/lib/blocks.hpp>

template<ConsensusType CT>
MessageHandler<CT>::MessageHandler()
{}

template<ConsensusType CT>
void MessageHandler<CT>::OnMessage(const MessagePtr & message, const Seconds & seconds)
{
    OnMessage(message, Clock::now() + seconds);
}

template<ConsensusType CT>
void MessageHandler<CT>::OnMessage(const MessagePtr & message, const TimePoint & tp)
{
    auto hash = message->Hash();
    // TODO: implement GetHash to avoid repeated hashing for MB/E

    std::lock_guard<std::mutex> lock(_mutex);
    if(_entries. template get<1>().find(hash) != _entries. template get<1>().end())
    {
        LOG_WARN(_log) << "MessageHandler<" << ConsensusToName(CT)
                       << ">::OnMessage - Ignoring duplicate message with hash: " << hash.to_string();
        return;
    }

    // For MB/EB, persistence manager (Backup) / Archiver (Primary) checks guarantee that messages arrive
    // in ascending epoch + sequence number combination order
    LOG_DEBUG (_log) << "MessageHandler<" << ConsensusToName(CT) << ">::OnMessage - timeout is " << tp << ", "
                     << message->ToJson();
    _entries.push_back(Entry{hash, message, tp});
}

template<ConsensusType CT>
typename MessageHandler<CT>::MessagePtr MessageHandler<CT>::GetFront()
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _entries.begin();
    if (it == _entries.end()) return nullptr;
    return it->block;
}

template<ConsensusType CT>
void MessageHandler<CT>::OnPostCommit(std::shared_ptr<PrePrepare> block)
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
        LOG_WARN (_log) << "MessageHandler<" << ConsensusToName(CT) << ">::OnPostCommit - already erased: " << hash.to_string();
    }
}

template<ConsensusType CT>
bool MessageHandler<CT>::PrimaryEmpty()
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto &expiration_index = _entries. template get<2>();
    auto it = expiration_index.lower_bound(Min_DT);
    auto end = expiration_index.upper_bound(Clock::now());
    return it == end;
}

template<ConsensusType CT>
auto MessageHandler<CT>::GetImminentTimeout() -> const TimePoint &
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto &expiration_index = _entries. template get<2>();
    auto it = expiration_index.lower_bound(Clock::now());
    if (it == expiration_index.end()) return Min_DT;
    return it->expiration;
}

template<ConsensusType CT>
bool MessageHandler<CT>::Contains(const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _entries. template get<1>();

    return hashed.find(hash) != hashed.end();
}

template<ConsensusType CT>
void MessageHandler<CT>::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _entries.erase(_entries.begin(), _entries.end());
}

/// Below are benchmarking methods, deprecated for now

template<ConsensusType CT>
bool MessageHandler<CT>::BatchFull()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return true;
}

template<ConsensusType CT>
bool MessageHandler<CT>::Empty()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _entries.empty();
}

/// above is deprecated

template class MessageHandler<ConsensusType::Request>;
template class MessageHandler<ConsensusType::MicroBlock>;
template class MessageHandler<ConsensusType::Epoch>;


void RequestMessageHandler::OnPostCommit(std::shared_ptr<PrePrepareMessage<ConsensusType::Request>> block)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & hashed = _entries. template get<1>();

    for(uint64_t pos = 0; pos < block->requests.size(); ++pos)
    {
        auto hash = block->requests[pos]->GetHash();

        if(hashed.find(hash) != hashed.end())
        {
            hashed.erase(hash);
        }
    }
}

void RequestMessageHandler::MoveToTarget(RequestInternalQueue & queue, size_t size)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto &expiration_index = _entries. template get<2>();
    auto end = expiration_index.upper_bound(Clock::now());  // strictly greater than

    for(auto pos = expiration_index.lower_bound(Min_DT); pos != end && size != 0; size--)
    {
        LOG_DEBUG(_log) << "RequestMessageHandler::MoveToTarget - moving " << pos->block->ToJson();
        // keep inserting and removing until at capacity or end
        queue.PushBack(std::static_pointer_cast<Request>(pos->block));
        pos = expiration_index.erase(pos);
    }
    // finally add empty delimiter to signify end of batch
    queue.PushBack(std::shared_ptr<Request>(new Request()));
}

bool MicroBlockMessageHandler::GetQueuedSequence(EpochSeq & epoch_seq)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto & sequence = _entries. template get<0>();
    auto it = sequence.end();
    if (it == sequence.begin())
    {
        epoch_seq.first = 0;
        epoch_seq.second = 0;
        return false;
    }
    // get last element
    it--;
    epoch_seq.first = it->block->epoch_number;
    epoch_seq.second = it->block->sequence;
    return true;
}
