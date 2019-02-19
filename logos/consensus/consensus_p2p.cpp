#include <logos/consensus/consensus_p2p.hpp>
#include <logos/consensus/messages/util.hpp>

#define P2P_BATCH_VERSION	2

constexpr unsigned P2P_MSG_SIZE_SIZE = sizeof(uint32_t);
constexpr unsigned P2P_BATCH_N_MSG = 2;

struct P2pBatchHeader {
    uint8_t batch_version;
    uint8_t logos_version;
    ConsensusType consensus_type;
    uint8_t delegate_id;
};

template<ConsensusType CT>
ConsensusP2pOutput<CT>::ConsensusP2pOutput(p2p_interface & p2p,
                                           uint8_t delegate_id)
    : _p2p(p2p)
    , _delegate_id(delegate_id)
{}

template<ConsensusType CT>
void ConsensusP2pOutput<CT>::AddMessageToBatch(const uint8_t *data, uint32_t size)
{
    uint32_t oldsize = _p2p_batch.size();
    uint32_t aligned_size = (size + P2P_MSG_SIZE_SIZE - 1) & ~(P2P_MSG_SIZE_SIZE - 1);

    _p2p_batch.resize(oldsize + aligned_size + P2P_MSG_SIZE_SIZE);
    memcpy(&_p2p_batch[oldsize], &size, P2P_MSG_SIZE_SIZE);
    memcpy(&_p2p_batch[oldsize + P2P_MSG_SIZE_SIZE], data, size);
    if (aligned_size > size)
    {
        memset(&_p2p_batch[oldsize + size + P2P_MSG_SIZE_SIZE], 0, aligned_size - size);
    }

    LOG_DEBUG(_log) << "ConsensusP2pOutput<" << ConsensusToName(CT)
                    << "> - message of size " << size
                    << " and type " << (unsigned)_p2p_batch[oldsize + P2P_MSG_SIZE_SIZE + 1]
                    << " added to p2p batch to delegate " << (unsigned)_delegate_id;
}

template<ConsensusType CT>
void ConsensusP2pOutput<CT>::CleanBatch()
{
    _p2p_batch.clear();
}

template<ConsensusType CT>
bool ConsensusP2pOutput<CT>::PropagateBatch()
{
    bool res = _p2p.PropagateMessage(&_p2p_batch[0], _p2p_batch.size(), true);

    if (res)
    {
        LOG_INFO(_log) << "ConsensusP2pOutput<" << ConsensusToName(CT)
                       << "> - p2p batch of size " << _p2p_batch.size()
                       << " propagated to delegate " << (unsigned)_delegate_id << ".";
    }
    else
    {
        LOG_ERROR(_log) << "ConsensusP2pOutput<" << ConsensusToName(CT)
                        << "> - p2p batch not propagated to delegate " << (unsigned)_delegate_id << ".";
    }

    CleanBatch();

    return res;
}

template<ConsensusType CT>
bool ConsensusP2pOutput<CT>::ProcessOutputMessage(const uint8_t *data, uint32_t size)
{
    CleanBatch();

    const struct P2pBatchHeader head =
    {
        .batch_version = P2P_BATCH_VERSION,
        .logos_version = logos_version,
        .consensus_type = CT,
        .delegate_id = _delegate_id,
    };

    AddMessageToBatch((uint8_t *)&head, sizeof(head));
    AddMessageToBatch(data, size);

    return PropagateBatch();
}

template<ConsensusType CT>
ConsensusP2p<CT>::ConsensusP2p(p2p_interface & p2p,
                               std::function<bool (const PostCommittedBlock<CT> &, uint8_t, ValidationStatus *)> Validate,
                               std::function<void (const PostCommittedBlock<CT> &, uint8_t)> ApplyUpdates,
                               std::function<bool (const PostCommittedBlock<CT> &)> BlockExists)
    : _p2p(p2p)
    , _Validate(Validate)
    , _ApplyUpdates(ApplyUpdates)
    , _BlockExists(BlockExists)
{}

template<>
bool ConsensusP2p<ConsensusType::Request>::ApplyCacheUpdates(
        const PostCommittedBlock<ConsensusType::Request> & block,
        std::shared_ptr<PostCommittedBlock<ConsensusType::Request>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status);

template<>
bool ConsensusP2p<ConsensusType::MicroBlock>::ApplyCacheUpdates(
        const PostCommittedBlock<ConsensusType::MicroBlock> & block,
        std::shared_ptr<PostCommittedBlock<ConsensusType::MicroBlock>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status);

template<>
bool ConsensusP2p<ConsensusType::Epoch>::ApplyCacheUpdates(
        const PostCommittedBlock<ConsensusType::Epoch> & block,
        std::shared_ptr<PostCommittedBlock<ConsensusType::Epoch>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status);

template<ConsensusType CT>
void ConsensusP2p<CT>::RetryValidate(const logos::block_hash &hash)
{
    _cache_mutex.lock();

    if (!_cache.count(hash))
    {
        _cache_mutex.unlock();
        return;
    }

    std::vector<std::pair<logos::block_hash,std::pair<uint8_t,std::shared_ptr<PostCommittedBlock<CT>>>>> cache_copy;
    auto range = _cache.equal_range(hash);

    for (auto it = range.first; it != range.second; it++)
    {
        cache_copy.push_back(*it);
    }

    _cache.erase(range.first, range.second);
    _cache_mutex.unlock();

    for (int i = 0; i < cache_copy.size(); ++i)
    {
        ValidationStatus status;
        auto value = cache_copy[i];
        const PostCommittedBlock<CT> &block = *value.second.second;
        if (_Validate(block, value.second.first, &status))
        {
            status.reason = logos::process_result::progress;
        }
        ApplyCacheUpdates(block, value.second.second, value.second.first, status);
    }
}		

template<ConsensusType CT>
void ConsensusP2p<CT>::CacheInsert(
        const logos::block_hash & hash,
        uint8_t delegate_id,
        const PostCommittedBlock<CT> & block,
        std::shared_ptr<PostCommittedBlock<CT>> & pblock)
{
    if (!pblock)
    {
        pblock = std::make_shared<PostCommittedBlock<CT>>(block);
    }
    std::lock_guard<std::mutex> lock(_cache_mutex);
    _cache.insert(std::make_pair(hash, std::make_pair(delegate_id, pblock)));
}

template<>
bool ConsensusP2p<ConsensusType::Request>::ApplyCacheUpdates(
        const PostCommittedBlock<ConsensusType::Request> & block,
        std::shared_ptr<PostCommittedBlock<ConsensusType::Request>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status)
{
    switch(status.reason)
    {
        case logos::process_result::progress:
            _ApplyUpdates(block, delegate_id);
            _container->RetryValidate(block.Hash());

            for(uint32_t i = 0; i < block.requests.size(); ++i)
            {
                _container->RetryValidate(block.requests[i]->Hash());
            }
            return true;

        case logos::process_result::gap_previous:
            CacheInsert(block.previous, delegate_id, block, pblock);
            return false;

        case logos::process_result::invalid_request:
            for(uint32_t i = 0; i < block.requests.size(); ++i)
            {
                if (status.requests[i] == logos::process_result::gap_previous)
                {
                    CacheInsert(block.requests[i]->previous, delegate_id, block, pblock);
                }
            }
            return false;

        default:
            return false;
    }
}

template<>
bool ConsensusP2p<ConsensusType::MicroBlock>::ApplyCacheUpdates(
        const PostCommittedBlock<ConsensusType::MicroBlock> & block,
        std::shared_ptr<PostCommittedBlock<ConsensusType::MicroBlock>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status)
{
    switch(status.reason)
    {
        case logos::process_result::progress:
            _ApplyUpdates(block, delegate_id);
            _container->RetryValidate(block.Hash());
            return true;

        case logos::process_result::gap_previous:
            CacheInsert(block.previous, delegate_id, block, pblock);
            return false;

        case logos::process_result::invalid_request:
            for(uint32_t i = 0; i < NUM_DELEGATES; ++i)
            {
                if (status.requests[i] == logos::process_result::gap_previous)
                {
                    CacheInsert(block.tips[i], delegate_id, block, pblock);
                }
            }
            return false;

        default:
            return false;
    }
}

template<>
bool ConsensusP2p<ConsensusType::Epoch>::ApplyCacheUpdates(
        const PostCommittedBlock<ConsensusType::Epoch> & block,
        std::shared_ptr<PostCommittedBlock<ConsensusType::Epoch>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status)
{
    switch(status.reason)
    {
        case logos::process_result::progress:
            _ApplyUpdates(block, delegate_id);
            _container->RetryValidate(block.Hash());
            return true;

        case logos::process_result::gap_previous:
            CacheInsert(block.previous, delegate_id, block, pblock);
            return false;

        case logos::process_result::invalid_tip:
            CacheInsert(block.micro_block_tip, delegate_id, block, pblock);
            return false;

        default:
            return false;
    }
}

template<ConsensusType CT>
bool ConsensusP2p<CT>::deserialize(const uint8_t *data, uint32_t size, PostCommittedBlock<CT> &block)
{
    if (size < MessagePrequelSize)
    {
        return false;
    }
    logos::bufferstream stream(data + MessagePrequelSize, size - MessagePrequelSize);
    bool error = false;
    new(&block) PostCommittedBlock<CT>(error, stream, logos_version, true, true);
    return !error;
}

template<ConsensusType CT>
bool ConsensusP2p<CT>::ProcessInputMessage(const uint8_t * data, uint32_t size)
{
    MessageType mtype = MessageType::Pre_Prepare;
    PostCommittedBlock<CT> block;
    std::shared_ptr<PostCommittedBlock<CT>> pblock;
    ValidationStatus status;
    uint8_t delegate_id;
    int mess_counter = 0;

    LOG_INFO(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                   << "> - received batch of size " << size;

    while (size >= P2P_MSG_SIZE_SIZE)
    {
        uint32_t msize = *(uint32_t *)data;
        data += P2P_MSG_SIZE_SIZE;
        size -= P2P_MSG_SIZE_SIZE;
        if (msize > size)
        {
            size = 1;
            break;
        }

        LOG_DEBUG(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                        << "> - message of size " << msize
                        << " and type " << (unsigned)data[1]
                        << " extracted from p2p batch";

        switch (mtype)
        {
            case MessageType::Pre_Prepare:
            {
                P2pBatchHeader *head = (P2pBatchHeader *)data;
                if (msize != sizeof(P2pBatchHeader)
                        || head->batch_version != P2P_BATCH_VERSION
                        || head->logos_version != logos_version
                        || head->consensus_type != CT)
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error parsing p2p batch header";
                    return false;
                }

                delegate_id = head->delegate_id;
                LOG_DEBUG(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                << "> - primary delegate id " << (unsigned)delegate_id
                                << " extracted from p2p batch";
                mtype = MessageType::Post_Committed_Block;
                break;
            }
            case MessageType::Post_Committed_Block:
            {
                if (!deserialize(data, msize, block))
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error deserialization PostCommittedBlock";
                    return false;
                }

                LOG_TRACE(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                << "> - PostCommittedBlock: deserialization done";

                if (block.type != mtype || block.consensus_type != CT)
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error parsing PostCommittedBlock";
                    return false;
                }

                LOG_TRACE(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                << "> - PostCommittedBlock: parsing done";

                if (_BlockExists(block))
                {
                    LOG_WARN(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                   << "> - stop validate block, it already exists in the storage";
                    return true;
                }

                LOG_TRACE(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                << "> - PostCommittedBlock: not exists";

                if (!_Validate(block, delegate_id, &status))
                {
                    if (status.reason != logos::process_result::gap_previous
                        && status.reason != logos::process_result::invalid_tip
                        && status.reason != logos::process_result::invalid_request)
                    {
                        LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                        << "> - error validation PostCommittedBlock: "
                                        << ProcessResultToString(status.reason);
                        return false;
                    }
                    else
                    {
                        LOG_TRACE(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                        << "> - validation PostCommittedBlock failed, try add to cache: "
                                        << ProcessResultToString(status.reason);
                    }
                }
                else
                {
                    status.reason = logos::process_result::progress;
                }

                LOG_TRACE(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                << "> - PostCommittedBlock: validation done";

                mtype = MessageType::Unknown;
                break;
            }
            default:
                break;
        }

        msize = (msize + P2P_MSG_SIZE_SIZE - 1) & ~(P2P_MSG_SIZE_SIZE - 1);
        data += msize;
        size -= msize;

        if (++mess_counter == P2P_BATCH_N_MSG)
        {
            break;
        }
    }

    if (size || mess_counter != P2P_BATCH_N_MSG)
    {
        LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                        << "> - error parsing p2p batch";
        return false;
    }
    else if (ApplyCacheUpdates(block, pblock, delegate_id, status))
    {
        LOG_INFO(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                       << "> - PostCommittedBlock with primary delegate " << (unsigned)delegate_id
                       << " saved to storage.";
        return true;
    }
    else
    {
        LOG_WARN(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                       << "> - PostCommittedBlock with primary delegate " << (unsigned)delegate_id
                       << " added to cache.";
        return true;
    }
}

bool ContainerP2p::ProcessInputMessage(const void *data, uint32_t size)
{
    if (size < P2P_MSG_SIZE_SIZE + sizeof(P2pBatchHeader))
        return false;

    switch (((P2pBatchHeader *)((uint32_t *)data + 1))->consensus_type)
    {
        case ConsensusType::Request:
            return _batch.ProcessInputMessage(data, size);
        case ConsensusType::MicroBlock:
            return _micro.ProcessInputMessage(data, size);
        case ConsensusType::Epoch:
            return _epoch.ProcessInputMessage(data, size);
        default:
            break;
    }

    return false;
}

int ContainerP2p::get_peers(int session_id, vector<logos::endpoint> & nodes, uint8_t count)
{
    std::lock_guard<std::mutex> lock(_sessions_mutex);

    if (session_id == P2P_GET_PEER_NEW_SESSION)
    {
        session_id = _session_id++;
        _sessions[session_id] = 0;
    }

    int next = _sessions[session_id];
    char *str_nodes[256];

    count = _p2p.get_peers(&next, str_nodes, count);
    _sessions[session_id] = next;

    for (uint8_t i = 0; i < count; ++i)
    {
        char *str = str_nodes[i];
        char *ptr = strrchr(str, ':');
        unsigned short port = 0;

        if (ptr)
        {
            sscanf(ptr + 1, "%hu", &port);
            *ptr = 0;
        }

        ptr = str;
        if (*ptr == '[')
        {
            ptr++;
            ptr[strlen(ptr) - 1] = 0;
        }

        boost::asio::ip::address addr = boost::asio::ip::address::from_string(ptr);
        logos::endpoint point(addr, port);
        nodes.push_back(point);
        free(str);
    }

    return session_id;
}

void ContainerP2p::close_session(int session_id)
{
    std::lock_guard<std::mutex> lock(_sessions_mutex);
    _sessions.erase(session_id);
}

void ContainerP2p::add_to_blacklist(const logos::endpoint & e)
{
    _p2p.add_to_blacklist(e.address().to_string().c_str());
}

bool ContainerP2p::is_blacklisted(const logos::endpoint & e)
{
    return _p2p.is_blacklisted(e.address().to_string().c_str());
}

template class ConsensusP2pOutput<ConsensusType::Request>;
template class ConsensusP2pOutput<ConsensusType::MicroBlock>;
template class ConsensusP2pOutput<ConsensusType::Epoch>;

template class ConsensusP2p<ConsensusType::Request>;
template class ConsensusP2p<ConsensusType::MicroBlock>;
template class ConsensusP2p<ConsensusType::Epoch>;
