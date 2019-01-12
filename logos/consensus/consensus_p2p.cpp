#include <logos/consensus/consensus_p2p.hpp>
#include <logos/consensus/messages/util.hpp>

#define P2P_BATCH_VERSION	1

constexpr unsigned P2P_MSG_SIZE_SIZE = sizeof(uint32_t);
constexpr unsigned P2P_BATCH_N_MSG = 4;

struct P2pBatchHeader {
    uint8_t version;
    MessageType type;
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
bool ConsensusP2pOutput<CT>::ProcessOutputMessage(const uint8_t *data, uint32_t size, MessageType mtype)
{
    bool res = true;

    if (mtype == MessageType::Pre_Prepare)
    {
        CleanBatch();

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

    if (mtype == MessageType::Post_Commit)
    {
        res = PropagateBatch();
    }

    return res;
}

template<ConsensusType CT>
ConsensusP2p<CT>::ConsensusP2p(p2p_interface & p2p,
                               std::function<bool (const Prequel &, MessageType, uint8_t, ValidationStatus *)> Validate,
                               std::function<void (const PrePrepareMessage<CT> &, uint8_t)> ApplyUpdates,
                               std::function<bool (const PrePrepareMessage<CT> &)> BlockExists)
    : _p2p(p2p)
    , _Validate(Validate)
    , _ApplyUpdates(ApplyUpdates)
    , _BlockExists(BlockExists)
{}

template<>
bool ConsensusP2p<ConsensusType::BatchStateBlock>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::BatchStateBlock> & block,
        std::shared_ptr<PrePrepareMessage<ConsensusType::BatchStateBlock>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status);

template<>
bool ConsensusP2p<ConsensusType::MicroBlock>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::MicroBlock> & block,
        std::shared_ptr<PrePrepareMessage<ConsensusType::MicroBlock>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status);

template<>
bool ConsensusP2p<ConsensusType::Epoch>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::Epoch> & block,
        std::shared_ptr<PrePrepareMessage<ConsensusType::Epoch>> pblock,
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

    std::vector<std::pair<logos::block_hash,std::pair<uint8_t,std::shared_ptr<PrePrepareMessage<CT>>>>> cache_copy;
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
        const PrePrepareMessage<CT> &block = *value.second.second;
        _Validate((const Prequel &)block, MessageType::Pre_Prepare, value.second.first, &status);
        ApplyCacheUpdates(block, value.second.second, value.second.first, status);
    }
}		

template<ConsensusType CT>
void ConsensusP2p<CT>::CacheInsert(
        const logos::block_hash & hash,
        uint8_t delegate_id,
        const PrePrepareMessage<CT> & block,
        std::shared_ptr<PrePrepareMessage<CT>> & pblock)
{
    if (!pblock)
    {
        pblock = std::make_shared<PrePrepareMessage<CT>>(block);
    }
    std::lock_guard<std::mutex> lock(_cache_mutex);
    _cache.insert(std::make_pair(hash, std::make_pair(delegate_id, pblock)));
}

template<>
bool ConsensusP2p<ConsensusType::BatchStateBlock>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::BatchStateBlock> & block,
        std::shared_ptr<PrePrepareMessage<ConsensusType::BatchStateBlock>> pblock,
        uint8_t delegate_id,
        ValidationStatus &status)
{
    switch(status.reason)
    {
        case logos::process_result::progress:
            _ApplyUpdates(block, delegate_id);
            _container->RetryValidate(block.Hash());

            for(uint32_t i = 0; i < block.block_count; ++i)
            {
                _container->RetryValidate(block.blocks[i].hash());
            }
            return true;

        case logos::process_result::gap_previous:
            CacheInsert(block.previous, delegate_id, block, pblock);
            return false;

        case logos::process_result::invalid_request:
            for(uint32_t i = 0; i < block.block_count; ++i)
            {
                if (status.requests[i] == logos::process_result::gap_previous)
                {
                    CacheInsert(block.blocks[i].hashables.previous, delegate_id, block, pblock);
                }
            }
            return false;

        default:
            return false;
    }
}

template<>
bool ConsensusP2p<ConsensusType::MicroBlock>::ApplyCacheUpdates(
        const PrePrepareMessage<ConsensusType::MicroBlock> & block,
        std::shared_ptr<PrePrepareMessage<ConsensusType::MicroBlock>> pblock,
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
        const PrePrepareMessage<ConsensusType::Epoch> & block,
        std::shared_ptr<PrePrepareMessage<ConsensusType::Epoch>> pblock,
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
    std::shared_ptr<PrePrepareMessage<CT>> pblock;
    ValidationStatus pre_status;
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

                if (_BlockExists(block))
                {
                    LOG_WARN(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - stop validate block, it already exists in the storage";
                    return false;
                }

                if (!_Validate((const Prequel &)*head, MessageType::Pre_Prepare, delegate_id, &pre_status))
                {
                    LOG_ERROR(_log) << "ConsensusP2p<" << ConsensusToName(CT)
                                    << "> - error validation p2p batch Pre_Prepare message: "
                                    << ProcessResultToString(pre_status.reason);
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
                                    << "> - error validation p2p batch Post_Prepare message: "
                                    << ProcessResultToString(status.reason);
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
                                    << "> - error validation p2p batch Post_Commit message: "
                                    << ProcessResultToString(status.reason);
                    // return false;
                }
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
    else if (ApplyCacheUpdates(block, pblock, delegate_id, pre_status))
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

bool ContainerP2p::ProcessInputMessage(const void *data, uint32_t size)
{
    if (size < P2P_MSG_SIZE_SIZE + sizeof(P2pBatchHeader))
        return false;

    switch (((P2pBatchHeader *)((uint32_t *)data + 1))->consensus_type)
    {
        case ConsensusType::BatchStateBlock:
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

template class ConsensusP2pOutput<ConsensusType::BatchStateBlock>;
template class ConsensusP2pOutput<ConsensusType::MicroBlock>;
template class ConsensusP2pOutput<ConsensusType::Epoch>;

template class ConsensusP2p<ConsensusType::BatchStateBlock>;
template class ConsensusP2p<ConsensusType::MicroBlock>;
template class ConsensusP2p<ConsensusType::Epoch>;
