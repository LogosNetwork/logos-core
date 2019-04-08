#include <logos/consensus/p2p/consensus_p2p.hpp>
#include <logos/consensus/messages/util.hpp>

ConsensusP2pOutput::ConsensusP2pOutput(p2p_interface & p2p,
                                       uint8_t delegate_id)
    : _p2p(p2p)
    , _delegate_id(delegate_id)
{}

void ConsensusP2pOutput::AddMessageToBuffer(const uint8_t *data,
                                            uint32_t size,
                                            MessageType message_type,
                                            uint32_t epoch_number,
                                            uint8_t dest_delegate_id)
{
    P2pHeader p2pheader={logos_version, P2pAppType::Consensus};
    auto hdrs_size = P2pHeader::HEADER_SIZE + P2pConsensusHeader::HEADER_SIZE;
    _p2p_buffer.resize(size + hdrs_size);
    uint8_t src_delegate_id = _delegate_id;
    if (message_type == MessageType::Post_Committed_Block)
    {
        src_delegate_id = 0xff;
        dest_delegate_id = 0xff;
    }
    P2pConsensusHeader header = {epoch_number, src_delegate_id, dest_delegate_id};
    std::vector<uint8_t> buf;
    {
        logos::vectorstream stream(buf);
        assert(p2pheader.Serialize(stream) == P2pHeader::HEADER_SIZE);
        assert(header.Serialize(buf) == P2pConsensusHeader::HEADER_SIZE);
    }
    memcpy(_p2p_buffer.data(), buf.data(), buf.size());
    memcpy(_p2p_buffer.data() + hdrs_size, data, size);

    LOG_DEBUG(_log) << "ConsensusP2pOutput"
                    << " - message type " << MessageToName(message_type)
                    << ", size " << size
                    << " is added to p2p to delegate " << (unsigned)_delegate_id;
}

void ConsensusP2pOutput::Clean()
{
    _p2p_buffer.clear();
}

bool ConsensusP2pOutput::Propagate()
{
    bool res = _p2p.PropagateMessage(&_p2p_buffer[0], _p2p_buffer.size(), true);

    if (res)
    {
        LOG_INFO(_log) << "ConsensusP2pOutput"
                       << " - p2p of size " << _p2p_buffer.size()
                       << " propagated to delegate " << (unsigned)_delegate_id << ".";
    }
    else
    {
        LOG_ERROR(_log) << "ConsensusP2pOutput"
                        << " - p2p not propagated to delegate " << (unsigned)_delegate_id << ".";
    }

    Clean();

    return res;
}

bool ConsensusP2pOutput::ProcessOutputMessage(const uint8_t *data,
                                              uint32_t size,
                                              MessageType message_type,
                                              uint32_t epoch_number,
                                              uint8_t dest_delegate_id)
{
    Clean();

    AddMessageToBuffer(data, size, message_type, epoch_number, dest_delegate_id);

    return Propagate();
}

template<ConsensusType CT>
ConsensusP2p<CT>::ConsensusP2p(p2p_interface & p2p,
                               std::function<bool (const PostCommittedBlock<CT> &, uint8_t, ValidationStatus *)> Validate,
                               std::function<void (const PostCommittedBlock<CT> &, uint8_t)> ApplyUpdates,
                               std::function<bool (const PostCommittedBlock<CT> &)> BlockExists)
    : _p2p(p2p)
      /* In the unit test we redefine the _RetryValidate function, but the redefinition calls this initial version
         of this function, see the file unit_test/p2p_test.cpp, the test VerifyCache. */
    , _RetryValidate([this](const logos::block_hash &hash)
        {
            this->RetryValidate(hash);
        })
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
            _RetryValidate(block.Hash());

            for(uint32_t i = 0; i < block.requests.size(); ++i)
            {
                _RetryValidate(block.requests[i]->Hash());
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
            _RetryValidate(block.Hash());
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
            _RetryValidate(block.Hash());
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
bool ConsensusP2p<CT>::Deserialize(const uint8_t *data, uint32_t size,
                                   uint8_t version, PostCommittedBlock<CT> &block)
{
    logos::bufferstream stream(data, size);
    bool error = false;
    new(&block) PostCommittedBlock<CT>(error, stream, version, true, true);
    return !error;
}

template<ConsensusType CT>
bool ConsensusP2p<CT>::ProcessInputMessage(const Prequel &prequel, const uint8_t * data, uint32_t size)
{
    MessageType mtype = MessageType::Pre_Prepare;
    PostCommittedBlock<CT> block;
    std::shared_ptr<PostCommittedBlock<CT>> pblock;
    ValidationStatus status;

    LOG_INFO(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                   << "> - received message of size " << size;

    if (!Deserialize(data, size, prequel.version, block))
    {
        LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                        << "> - error deserialization PostCommittedBlock";
        return false;
    }

    LOG_TRACE(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                    << "> - PostCommittedBlock: deserialization done";

    if (block.consensus_type != CT)
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

    if (!_Validate(block, block.primary_delegate, &status))
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

    if (ApplyCacheUpdates(block, pblock, block.primary_delegate, status))
    {
        LOG_INFO(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                       << "> - PostCommittedBlock with primary delegate " << (unsigned)block.primary_delegate
                       << ", epoch number " << block.epoch_number
                       << " saved to storage (or already persisted).";
        return true;
    }
    else
    {
        LOG_WARN(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                       << "> - PostCommittedBlock with primary delegate " << (unsigned)block.primary_delegate
                       << " added to cache.";
        return true;
    }
}

bool ContainerP2p::ProcessInputMessage(const Prequel &prequel, const void *data, uint32_t size)
{
    switch (prequel.consensus_type)
    {
        case ConsensusType::Request:
            return _batch.ProcessInputMessage(prequel, data, size);
        case ConsensusType::MicroBlock:
            return _micro.ProcessInputMessage(prequel, data, size);
        case ConsensusType::Epoch:
            return _epoch.ProcessInputMessage(prequel, data, size);
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

template class ConsensusP2p<ConsensusType::Request>;
template class ConsensusP2p<ConsensusType::MicroBlock>;
template class ConsensusP2p<ConsensusType::Epoch>;
