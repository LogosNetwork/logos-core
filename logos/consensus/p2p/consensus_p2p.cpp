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
    auto hdrs_size = P2pHeader::SIZE + P2pConsensusHeader::SIZE;
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
        assert(p2pheader.Serialize(stream) == P2pHeader::SIZE);
        assert(header.Serialize(stream) == P2pConsensusHeader::SIZE);
    }
    memcpy(_p2p_buffer.data(), buf.data(), buf.size());
    memcpy(_p2p_buffer.data() + hdrs_size, data, size);

    LOG_DEBUG(_log) << "ConsensusP2pOutput"
                    << " - message type " << MessageToName(message_type)
                    << ", size " << _p2p_buffer.size()
                    << ", epoch number " << epoch_number
                    << ", dest delegate id " << (int)dest_delegate_id
                    << " is added to p2p to delegate " << (unsigned)_delegate_id;
}

//void SerializeRequest(Request& request)
//{
//
//    std::vector<uint8_t> buf;
//    {
//        logos::stream stream(buf);
//        request.Serialize(stream);
//    }
//    SerializeRequest(buf.data(), buf.size());
//}
//
//void SerializeRequest(const uint8_t *data, uint32_t size)
//{
//    P2pHeader p2pheader={logos_version, P2pAppType::Request};
//    auto hdrs_size = P2pHeader::SIZE;
//    _p2p_buffer.resize(size + hdrs_size);
//
//    std::vector<uint8_t> buf;
//    {
//        logos::vectorstream stream(buf);
//        assert(p2pheader.Serialize(stream) == P2pHeader::SIZE);
//    }
//    memcpy(_p2p_buffer.data(), buf.data(), buf.size());
//    memcpy(_p2p_buffer.data() + hdrs_size, data, size);
//
//}
//
//void DeserializeRequest(const uint8_t *data, uint32_t size)
//{
//    logos::bufferstream(data, size);
//    bool error = false;
//    Request request(error, bufferstream);
//
//
//}

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
                               std::function<bool (const PostCommittedBlock<CT> &)> AddBlock)
    : _p2p(p2p)
    , _AddBlock(AddBlock)
{}

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
    PostCommittedBlock<CT> block;

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

    if (_AddBlock(block))
    {
        LOG_INFO(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                       << "> - PostCommittedBlock with primary delegate " << (unsigned)block.primary_delegate
                       << ", epoch number " << block.epoch_number
                       << " added to cache.";
        return true;
    }
    else
    {
        LOG_WARN(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                       << "> - PostCommittedBlock with primary delegate " << (unsigned)block.primary_delegate
                       << " has invalid signatures and rejected.";
        return false;
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
        _sessions.emplace(session_id, GetEndpointSession(session_id));
    }

    //int next = _sessions[session_id].next;
    char *str_nodes[256];
    int need = count;

    while(need > 0)
    {
        int got = _p2p.get_peers(&_sessions[session_id].next, str_nodes, need);
        //_sessions[session_id].next = next;

        for (uint8_t i = 0; i < got; ++i)
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
            free(str);

            if(_sessions[session_id].seen.find(point) == _sessions[session_id].seen.end())
            {
                nodes.push_back(point);
                _sessions[session_id].seen.insert(point);
                if(--need == 0)
                {
                    return session_id;
                }
            }
            else
            {
                return session_id;
            }
        }
/*TODO
        boost::asio::ip::address addr = boost::asio::ip::address::from_string(ptr);
        logos::endpoint point(addr, port);
        nodes.push_back(point);
        free(str);
*/
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
