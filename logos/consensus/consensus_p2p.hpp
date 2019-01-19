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

    bool ProcessOutputMessage(const uint8_t *data, uint32_t size);

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
                 std::function<bool (const PostCommittedBlock<CT> &, uint8_t, ValidationStatus *)> Validate,
                 std::function<void (const PostCommittedBlock<CT> &, uint8_t)> ApplyUpdates,
                 std::function<bool (const PostCommittedBlock<CT> &)> BlockExists);

    bool ProcessInputMessage(const uint8_t *data, uint32_t size);

    p2p_interface &                                                                             _p2p;

private:
    void RetryValidate(const logos::block_hash &hash);

    bool ApplyCacheUpdates(const PostCommittedBlock<CT> & block,
                           std::shared_ptr<PostCommittedBlock<CT>> pblock,
                           uint8_t delegate_id,
                           ValidationStatus &status);

    void CacheInsert(const logos::block_hash & hash,
                     uint8_t delegate_id,
                     const PostCommittedBlock<CT> & block,
                     std::shared_ptr<PostCommittedBlock<CT>> & pblock);

    bool deserialize(const uint8_t *data,
                     uint32_t size,
                     PostCommittedBlock<CT> &block);

    Log                                                                                         _log;
    std::function<bool (const PostCommittedBlock<CT> &, uint8_t, ValidationStatus *)>           _Validate;
    std::function<void (const PostCommittedBlock<CT> &, uint8_t)>                               _ApplyUpdates;
    std::function<bool (const PostCommittedBlock<CT> &)>                                        _BlockExists;
    std::multimap<logos::block_hash,std::pair<uint8_t,std::shared_ptr<PostCommittedBlock<CT>>>> _cache;
    std::mutex                                                                                  _cache_mutex;
    ContainerP2p *                                                                              _container;

    friend class ContainerP2p;
    friend class PersistenceP2p<CT>;
};

template<ConsensusType CT>
class PersistenceP2p
{
public:
    PersistenceP2p(p2p_interface & p2p,
                   logos::block_store &store)
        : _persistence(store, NonDelPersistenceManager<CT>::DEFAULT_CLOCK_DRIFT)
        , _p2p(p2p,
            [this](const PostCommittedBlock<CT> &message, uint8_t delegate_id, ValidationStatus * status)
            {
                const PrePrepareMessage<CT> block(message);
                return this->_persistence.ValidatePreprepare(block, status);
            },
            [this](const PostCommittedBlock<CT> &message, uint8_t delegate_id)
            {
                this->_persistence.ApplyUpdates(message, delegate_id);
            },
            [this](const PostCommittedBlock<CT> &message)
            {
                return this->_persistence.BlockExists(message);
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

constexpr int P2P_GET_PEER_NEW_SESSION = -1;

class ContainerP2p
{
public:
    ContainerP2p(p2p_interface & p2p,
                 logos::block_store &store)
        : _p2p(p2p)
        , _batch(p2p, store)
        , _micro(p2p, store)
        , _epoch(p2p, store)
        , _session_id(0)
    {
        _batch._p2p._container = this;
        _micro._p2p._container = this;
        _epoch._p2p._container = this;
    }

    bool ProcessInputMessage(const void *data, uint32_t size);

    /* Where session_id is initialized with an invalid value (-1) and a new session_id
     * is returned by the function, along with a list of peers. count indicates how
     * many peers we are asking for.
     * to be called on init call of bootstrap_peer() and to which we will subsequently
     * get peers at random from the vector.
     * The reason of adding the session when get_peers() is so that the caller doesn't
     * get repeated endpoints. To create a new session, the function is called with
     * an invalid session_id (e.g., #define GET_PEER_NEW_SESSION -1), and the function
     * should create a new session and return a valid session_id.
     */
    int get_peers(int session_id, vector<logos::endpoint> & nodes, uint8_t count);

    /* Close session (to be managed in bootstrap_attempt) */
    void close_session(int session_id);

    /* Add a peer to a blacklist
     * to be called when validation fails
     */
    void add_to_blacklist(const logos::endpoint & e);

    /* true if peer is in the blacklist
     * to be checked when we select a new peer to bootstrap from
     */
    bool is_blacklisted(const logos::endpoint & e);

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
    std::atomic<int>                                _session_id;

    template<ConsensusType CT>
    friend class ConsensusP2p;
    template<ConsensusType CT>
    friend class PersistenceP2p;
};
