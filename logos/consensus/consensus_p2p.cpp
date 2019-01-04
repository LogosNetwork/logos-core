#include <logos/consensus/consensus_p2p.hpp>
#include <logos/consensus/messages/util.hpp>

#define P2P_BATCH_VERSION	1

struct P2pBatchHeader {
    uint8_t version;
    MessageType type;
    ConsensusType consensus_type;
    uint8_t delegate_id;
    uint8_t padding;
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
    size_t oldsize = _p2p_batch.size();

    _p2p_batch.resize(oldsize + size + 4);
    memcpy(&_p2p_batch[oldsize], &size, 4);
    memcpy(&_p2p_batch[oldsize + 4], data, size);

    LOG_DEBUG(_log) << "ConsensusP2pOutput<" << ConsensusToName(CT)
                    << "> - message of size " << size
                    << " and type " << (unsigned)_p2p_batch[oldsize + 5]
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
    bool res = _p2p.PropagateMessage(&_p2p_batch[0], _p2p_batch.size());

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
bool ConsensusP2pOutput<CT>::ProcessOutputMessage(const uint8_t *data, uint32_t size, bool propagate)
{
    bool res = true;

    if (!_p2p_batch.size())
    {
        const struct P2pBatchHeader head =
        {
            .version = P2P_BATCH_VERSION,
            .type = MessageType::Unknown,
            .consensus_type = CT,
            .delegate_id = _delegate_id,
        };

        AddMessageToBatch((uint8_t *)&head, sizeof(head));
    }

    AddMessageToBatch(data, size);

    if (propagate)
    {
        res = PropagateBatch();
    }

    return res;
}

template<ConsensusType CT>
ConsensusP2p<CT>::ConsensusP2p(p2p_interface & p2p,
                               std::function<bool (const Prequel &, MessageType, uint8_t, ValidationStatus *)> Validate,
                               std::function<void (const PrePrepareMessage<CT> &, uint8_t)> ApplyUpdates)
    : _p2p(p2p)
    , _Validate(Validate)
    , _ApplyUpdates(ApplyUpdates)
{}

template<>
bool ConsensusP2p<ConsensusType::BatchStateBlock>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::BatchStateBlock> &message,
        uint8_t delegate_id,
        ValidationStatus &status);

template<>
bool ConsensusP2p<ConsensusType::MicroBlock>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::MicroBlock> &message,
        uint8_t delegate_id,
        ValidationStatus &status);

template<>
bool ConsensusP2p<ConsensusType::Epoch>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::Epoch> &message,
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

    std::vector<std::pair<logos::block_hash,std::pair<uint8_t,PrePrepareMessage<CT>>>> cache_copy;
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
        _Validate((const Prequel &)value.second.second, MessageType::Pre_Prepare, value.second.first, &status);
        ApplyCacheUpdates(value.second.second, value.second.first, status);
    }
}		

template<>
bool ConsensusP2p<ConsensusType::BatchStateBlock>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::BatchStateBlock> &message,
        uint8_t delegate_id,
        ValidationStatus &status)
{
    switch(status.reason)
    {
        case logos::process_result::progress:
            _ApplyUpdates(message, delegate_id);
            _container->RetryValidate(message.Hash());

            for(uint32_t i = 0; i < message.block_count; ++i)
            {
                _container->RetryValidate(message.blocks[i].hash());
            }
            return true;

        case logos::process_result::gap_previous:
            _cache_mutex.lock();
            _cache.insert(std::make_pair(message.previous, std::make_pair(delegate_id, message)));
            _cache_mutex.unlock();
            return false;

        case logos::process_result::invalid_request:
            for(uint32_t i = 0; i < message.block_count; ++i)
            {
                if (status.requests[i] == logos::process_result::gap_previous)
                {
                    _cache_mutex.lock();
                    _cache.insert(std::make_pair(message.blocks[i].hashables.previous,
                            std::make_pair(delegate_id, message)));
                    _cache_mutex.unlock();
                }
            }
            return false;

        default:
            return false;
    }
}

template<>
bool ConsensusP2p<ConsensusType::MicroBlock>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::MicroBlock> &message,
        uint8_t delegate_id,
        ValidationStatus &status)
{
    switch(status.reason)
    {
        case logos::process_result::progress:
            _ApplyUpdates(message, delegate_id);
            _container->RetryValidate(message.Hash());
            return true;

        case logos::process_result::gap_previous:
            _cache_mutex.lock();
            _cache.insert(std::make_pair(message.previous, std::make_pair(delegate_id, message)));
            _cache_mutex.unlock();
            return false;

        case logos::process_result::invalid_request:
            for(uint32_t i = 0; i < NUM_DELEGATES; ++i)
            {
                if (status.requests[i] == logos::process_result::gap_previous)
                {
                    _cache_mutex.lock();
                    _cache.insert(std::make_pair(message.tips[i],
                            std::make_pair(delegate_id, message)));
                    _cache_mutex.unlock();
                }
            }
            return false;

        default:
            return false;
    }
}

template<>
bool ConsensusP2p<ConsensusType::Epoch>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::Epoch> &message,
        uint8_t delegate_id,
        ValidationStatus &status)
{
    switch(status.reason)
    {
        case logos::process_result::progress:
            _ApplyUpdates(message, delegate_id);
            _container->RetryValidate(message.Hash());
            return true;

        case logos::process_result::gap_previous:
            _cache_mutex.lock();
            _cache.insert(std::make_pair(message.previous, std::make_pair(delegate_id, message)));
            _cache_mutex.unlock();
            return false;

        case logos::process_result::invalid_tip:
            _cache_mutex.lock();
            _cache.insert(std::make_pair(message.micro_block_tip, std::make_pair(delegate_id, message)));
            _cache_mutex.unlock();
            return false;

        default:
            return false;
    }
}

template<ConsensusType CT>
MessageHeader<MessageType::Pre_Prepare, CT>*
ConsensusP2p<CT>::deserialize(const uint8_t *data, uint32_t size, PrePrepareMessage<CT> &block)
{
    block = *(PrePrepareMessage<CT>*)data;
    return (PrePrepareMessage<CT>*)data;
}

template<>
MessageHeader<MessageType::Pre_Prepare, ConsensusType::BatchStateBlock>*
ConsensusP2p<ConsensusType::BatchStateBlock>::deserialize(const uint8_t *data, uint32_t size, PrePrepareMessage<ConsensusType::BatchStateBlock> &block)
{
	logos::bufferstream stream(data, size);
	bool error;
	return new(&block) BatchStateBlock(error, stream);
}

template<ConsensusType CT>
bool ConsensusP2p<CT>::ProcessInputMessage(const uint8_t * data, uint32_t size)
{
    MessageType mtype = MessageType::Unknown;
    PrePrepareMessage<CT> block;
    ValidationStatus pre_status;
    uint8_t delegate_id;
    int mess_counter = 0;

    LOG_INFO(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                   << "> - received batch of size " << size;

    while (size >= 4)
    {
        uint32_t msize = *(uint32_t *)data;
        data += 4;
        size -= 4;
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
            case MessageType::Unknown:
            {
                P2pBatchHeader *head = (P2pBatchHeader *)data;
                if (msize != sizeof(P2pBatchHeader)
                        || head->version != P2P_BATCH_VERSION
                        || head->type != mtype
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
                mtype = MessageType::Pre_Prepare;
                break;
            }
            case MessageType::Pre_Prepare:
            {
                MessageHeader<MessageType::Pre_Prepare,CT> *head = deserialize(data, size, block);
                if (head->type != mtype || head->consensus_type != CT)
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error parsing p2p batch Pre_Prepare message";
                    return false;
                }
                if (!_Validate((const Prequel &)*head, MessageType::Pre_Prepare, delegate_id, &pre_status))
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error validation p2p batch Pre_Prepare message: " <<
                    ProcessResultToString(pre_status.reason);
                    // return false;
                }
                mtype = MessageType::Post_Prepare;
                break;
            }
            case MessageType::Post_Prepare:
            {
                MessageHeader<MessageType::Post_Prepare,CT> *head
                        = (MessageHeader<MessageType::Post_Prepare,CT>*)data;
                if (head->type != mtype || head->consensus_type != CT)
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error parsing p2p batch Post_Prepare message";
                    return false;
                }

                ValidationStatus status;
                if (!_Validate((const Prequel &)*head, MessageType::Post_Prepare, delegate_id, &status))
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error validation p2p batch Post_Prepare message: " <<
                    ProcessResultToString(status.reason);
                    // return false;
                }
                mtype = MessageType::Post_Commit;
                break;
            }
            case MessageType::Post_Commit:
            {
                MessageHeader<MessageType::Post_Commit,CT> *head
                        = (MessageHeader<MessageType::Post_Commit,CT>*)data;
                if (head->type != mtype || head->consensus_type != CT)
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error parsing p2p batch Post_Commit message";
                    return false;
                }
                ValidationStatus status;
                if (!_Validate((const Prequel &)*head, MessageType::Post_Commit, delegate_id, &status))
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error validation p2p batch Post_Commit message: " <<
                    ProcessResultToString(status.reason);
                    // return false;
                }
                break;
            }
            default:
                break;
        }

        data += msize;
        size -= msize;

        if (++mess_counter == 4)
        {
            break;
        }
    }

    if (size || mess_counter != 4)
    {
        LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                        << "> - error parsing p2p batch";
        return false;
    }
    else if (ApplyCacheUpdates(block, delegate_id, pre_status))
    {
        LOG_INFO(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                       << "> - PrePrepare message with primary delegate " << (unsigned)delegate_id
                       << " saved to storage.";
        return true;
    }
    else
    {
        LOG_WARN(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                       << "> - PrePrepare message with primary delegate " << (unsigned)delegate_id
                       << " added to cache.";
        return true;
    }
}

template class ConsensusP2pOutput<ConsensusType::BatchStateBlock>;
template class ConsensusP2pOutput<ConsensusType::MicroBlock>;
template class ConsensusP2pOutput<ConsensusType::Epoch>;

template class ConsensusP2p<ConsensusType::BatchStateBlock>;
template class ConsensusP2p<ConsensusType::MicroBlock>;
template class ConsensusP2p<ConsensusType::Epoch>;
