#pragma once

#include <functional>
#include <map>

#include <logos/lib/log.hpp>
#include <logos/p2p/p2p.h>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager_incl.hpp>

template<ConsensusType CT>
class ConsensusP2pOutput
{
public:
    ConsensusP2pOutput(p2p_interface & p2p,
                       uint8_t delegate_id);

    bool ProcessOutputMessage(const uint8_t *data, uint32_t size, MessageType mtype);

    p2p_interface &         _p2p;

private:
    void CleanBatch();
    void AddMessageToBatch(const uint8_t *data, uint32_t size);
    bool PropagateBatch();

    Log                     _log;
    uint8_t                 _delegate_id;
    std::vector<uint8_t>    _p2p_batch;	// PrePrepare + PostPrepare + PostCommit
};

class ContainerP2p;

template<ConsensusType CT>
class PersistenceP2p;

template<ConsensusType CT>
class ConsensusP2p
{
public:
    ConsensusP2p(p2p_interface & p2p,
                 std::function<bool (const Prequel &, MessageType, uint8_t, ValidationStatus *)> Validate,
                 std::function<void (const PrePrepareMessage<CT> &, uint8_t)> ApplyUpdates);

    bool ProcessInputMessage(const uint8_t *data, uint32_t size);

    p2p_interface &                                                                 _p2p;

private:
    void RetryValidate(const logos::block_hash &hash);
    bool ApplyCacheUpdates(const PrePrepareMessage<CT> &message, uint8_t delegate_id, ValidationStatus &status);
    MessageHeader<MessageType::Pre_Prepare, CT>* deserialize(const uint8_t *data, uint32_t size, PrePrepareMessage<CT> &block);

    Log                                                                             _log;
    std::function<bool (const Prequel &, MessageType, uint8_t, ValidationStatus *)> _Validate;
    std::function<void (const PrePrepareMessage<CT> &, uint8_t)>                    _ApplyUpdates;
    std::multimap<logos::block_hash,std::pair<uint8_t,PrePrepareMessage<CT>>>       _cache;
    std::mutex                                                                      _cache_mutex;
    ContainerP2p *                                                                  _container;

    friend class ContainerP2p;
    friend class PersistenceP2p<CT>;
};

template<ConsensusType CT>
class PersistenceP2p
{
public:
    PersistenceP2p(p2p_interface & p2p,
                   logos::block_store &store)
        : _persistence(store)
        , _p2p(p2p,
            [this](const Prequel &message, MessageType mtype, uint8_t delegate_id, ValidationStatus * status)
            {
                return mtype == MessageType::Pre_Prepare  ? this->_persistence.Validate((PrePrepareMessage<CT> &)message, delegate_id, status)
                     : mtype == MessageType::Post_Prepare ? false /* todo */
                     : mtype == MessageType::Post_Commit  ? false /* todo */
                     : false;
            },
            [this](const PrePrepareMessage<CT> &message, uint8_t delegate_id)
            {
                this->_persistence.ApplyUpdates(message, delegate_id);
            }
        )
    {}

    bool ProcessInputMessage(const void *data, uint32_t size)
    {
        return _p2p.ProcessInputMessage((const uint8_t *)data, size);
    }

private:
    NonDelPersistenceManager<CT>    _persistence;
    ConsensusP2p<CT>                _p2p;

    friend class ContainerP2p;
    friend class ConsensusP2p<CT>;
};

class ContainerP2p
{
public:
    ContainerP2p(p2p_interface & p2p,
                 logos::block_store &store)
        : _p2p(p2p)
        , _batch(p2p, store)
        , _micro(p2p, store)
        , _epoch(p2p, store)
    {
        _batch._p2p._container = this;
        _micro._p2p._container = this;
        _epoch._p2p._container = this;
    }

    bool ProcessInputMessage(const void *data, uint32_t size);

    p2p_interface &                                 _p2p;
private:
    void RetryValidate(const logos::block_hash &hash)
    {
        _batch._p2p.RetryValidate(hash);
        _micro._p2p.RetryValidate(hash);
        _epoch._p2p.RetryValidate(hash);
    }

    PersistenceP2p<ConsensusType::BatchStateBlock>  _batch;
    PersistenceP2p<ConsensusType::MicroBlock>       _micro;
    PersistenceP2p<ConsensusType::Epoch>            _epoch;

    template<ConsensusType CT>
    friend class ConsensusP2p;
    template<ConsensusType CT>
    friend class PersistenceP2p;
};
