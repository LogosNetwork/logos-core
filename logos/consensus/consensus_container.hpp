/// @file
/// This file contains the declaration of the ConsensusContainer class, which encapsulates
/// consensus related types
#pragma once

#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/consensus/request/request_consensus_manager.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/network/epoch_peer_manager.hpp>
#include <logos/network/consensus_netio_manager.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/p2p/consensus_p2p.hpp>
#include <logos/epoch/epoch_transition.hpp>
#include <logos/tx_acceptor/tx_channel.hpp>

#include <queue>

namespace logos
{
    class node_config;
    class state_block;
    class node;
}

class Archiver;
class PeerAcceptorStarter;
class EpochManager;

/// Optimize access to the _cur_epoch in ConsensusContainer
/// It only needs to be locked during transition, for simplicity T-5min : T+20sec
class OptLock
{
public:
    OptLock(std::atomic<EpochTransitionState> &transition, std::mutex &mutex) : _locked(false), _mutex(mutex)
    {
        if (transition != EpochTransitionState::None)
        {
            _mutex.lock();
            _locked = true;
        }
    }
    ~OptLock()
    {
        if (_locked)
        {
            _mutex.unlock();
        }
    }

private:

    bool         _locked;
    std::mutex & _mutex;
};

class NewEpochEventHandler
{
public:
    NewEpochEventHandler() = default;
    virtual ~NewEpochEventHandler() = default;
    virtual void OnPostCommit(uint32_t epoch_number) = 0;
    virtual void OnPrePrepareRejected(EpochTransitionDelegate delegate) = 0;
    virtual bool IsRecall() = 0;
    virtual DelegateIdentityManager & GetIdentityManager() = 0;
};

class InternalConsensus
{
public:
    InternalConsensus() = default;
    virtual ~InternalConsensus() = default;
    virtual logos::process_return OnDelegateMessage(std::shared_ptr<DelegateMessage<ConsensusType::MicroBlock>>) = 0;
    virtual logos::process_return OnDelegateMessage(std::shared_ptr<DelegateMessage<ConsensusType::Epoch>>) = 0;
    virtual void EpochTransitionEventsStart() = 0;
};

class ConsensusScheduler
{
protected:
    using TimePoint  = boost::posix_time::ptime;

public:
    virtual void AttemptInitiateConsensus(ConsensusType CT) = 0;
    virtual void ScheduleTimer(ConsensusType CT, const TimePoint & timeout) = 0;
    virtual void CancelTimer(ConsensusType CT) = 0;
};

/// Binds accepted socket to the correct ConsensusNetIOManager
/// Exposes DelegateIdentityManager to make/validate AddressAd
class PeerBinder
{
protected:
    using Socket     = boost::asio::ip::tcp::socket;
    using Endpoint   = boost::asio::ip::tcp::endpoint;
public:
    PeerBinder() = default;
    virtual ~PeerBinder() = default;
    virtual DelegateIdentityManager & GetIdentityManager() = 0;
    virtual bool Bind(std::shared_ptr<Socket>,
                      const Endpoint endpoint,
                      uint32_t epoch_number,
                      uint8_t delegate_id) = 0;
};

/// Encapsulates consensus related objects.
///
/// This class serves as a container for ConsensusManagers
/// and other consensus-related types and provides an interface
/// to the node object.
class ConsensusContainer : public ConsensusScheduler,
                           public InternalConsensus,
                           public NewEpochEventHandler,
                           public TxChannelExt,
                           public PeerBinder
{
    friend class DelegateIdentityManager;

    using Service    = boost::asio::io_service;
    using Config     = logos::node_config;
    using Store      = logos::block_store;
    using Alarm      = logos::alarm;
    using Accounts   = AccountAddress[NUM_DELEGATES];
    using BindingMap = std::map<uint, std::shared_ptr<EpochManager>>;

    using Timer      = boost::asio::deadline_timer;
    using Error      = boost::system::error_code;

public:

    /// Class constructor.
    ///
    /// This must be a singleton instance.
    ///     @param[in] service reference to boost asio service
    ///     @param[in] store reference to blockstore
    ///     @param[in] alarm reference to alarm
    ///     @param[in] log reference to boost log
    ///     @param[in] config reference to node_config
    ///     @param[in] archiver epoch/microblock related consensus validation and persistence
    ConsensusContainer(Service & service,
                       Store & store,
                       logos::alarm & alarm,
                       const logos::node_config & config,
                       Archiver & archiver,
                       DelegateIdentityManager & identity_manager,
                       p2p_interface & p2p);

    ~ConsensusContainer() = default;

    /// Handles requests for request consensus.
    ///
    /// Submits requests to consensus logic.
    ///     @param[in] request Incoming request message
    ///     @param[in] should_buffer bool flag that, when set, will
    ///                              cause the block to be buffered
    ///     @return process_return result of the operation
    logos::process_return OnDelegateMessage(std::shared_ptr<DM> request,
                                            bool should_buffer) override;

    /// Handles requests for batch block consensus.
    ///
    /// Submits transactions to consensus logic.
    ///     @param[in] blocks state blocks containing the transaction
    ///     @return responses containinig process_result and hash
    Responses OnSendRequest(std::vector<std::shared_ptr<DM>> &blocks) override;

    /// Tells current epoch manager, if any, to initiate consensus of given type
    ///     @param[in] consensus type
    void AttemptInitiateConsensus(ConsensusType CT) override;

    // Schedule a future call to AttemptInitiateConsensus at `timeout`, if a current timer
    // doesn't exist or expires after `timeout`
    //      @param[in] consensus type
    //      @param[in] absolute time point at which callback is executed
    void ScheduleTimer(ConsensusType CT, const TimePoint &timeout) override;

    // Cancel a scheduled future call to AttemptInitiateConsensus at `timeout`, if one exists
    //      @param[in] consensus type
    void CancelTimer(ConsensusType CT) override;

    /// Called when buffering is done for batch block consensus.
    ///
    /// Indicates that the buffering of state blocks is complete.
    ///     @param[out] result result of the operation
    void BufferComplete(logos::process_return & result);

    /// Get current epoch id
    /// @returns current epoch id
    static uint32_t GetCurEpochNumber() { return _cur_epoch_number; }

    /// Binds connected socket to the correct delegates set, mostly applicable during epoch transition
    /// @param endpoint connected endpoing
    /// @param socket connected socket
    /// @param connection type of peer's connection
    bool Bind(std::shared_ptr<Socket>,
              const Endpoint endpoint,
              uint32_t epoch_number,
              uint8_t delegate_id) override;

    /// Get delegate identity manager reference
    /// @returns DelegateIdentityManager reference
    DelegateIdentityManager & GetIdentityManager() override
    {
        return _identity_manager;
    }

    /// Start Epoch Transition
    void EpochTransitionEventsStart() override;

    /// Receive message from p2p network
    bool OnP2pReceive(const void *message, size_t size);

    static bool ValidateSigConfig()
    {
        return _validate_sig_config;
    }

    /// Start consensus container
    void Start();

    PeerInfoProvider & GetPeerInfoProvider()
    {
        return _p2p;
    }

protected:

    /// Initiate MicroBlock consensus, internal request
    ///        @param[in] MicroBlock containing the batch blocks
    logos::process_return OnDelegateMessage(std::shared_ptr<DelegateMessage<ConsensusType::MicroBlock>> message) override;

    /// Initiate Epoch consensus, internal request
    ///        @param[in] Epoch containing the microblocks
    logos::process_return OnDelegateMessage(std::shared_ptr<DelegateMessage<ConsensusType::Epoch>> message) override;

private:

    /// Set current epoch id, this is done by the NodeIdentityManager on startup
    /// And by epoch transition logic
    /// @param id epoch id
    static void SetCurEpochNumber(uint32_t n) { _cur_epoch_number = n; }

    /// Epoch transition start event at T-20sec
    /// @param delegate_idx delegate's index [in]
    void EpochTransitionStart(uint8_t delegate_idx);

    /// Epoch start event at T(00:00)
    /// @param delegate_idx delegate's index [in]
    void EpochStart(uint8_t delegate_idx);

    /// Epoch start event at T+20sec
    /// @param delegate_idx delegate's index [in]
    void EpochTransitionEnd(uint8_t delegate_idx);

    /// Build consensus configuration
    /// @param delegate_idx delegate's index [in]
    /// @param delegates in the epoch [in]
    /// @returns delegate's configuration
    ConsensusManagerConfig BuildConsensusConfig(uint8_t delegate_idx, const ApprovedEB &epoch);

    /// Transition if received PostCommit with E#_i
    /// @param epoch_number PrePrepare epoch number
    /// @return true if no error
    void OnPostCommit(uint32_t epoch_number) override;

    /// Transition Retiring delegate to ForwardOnly
    /// @param delegate that received preprepare reject
    /// @return true if no error
    void OnPrePrepareRejected(EpochTransitionDelegate delegate) override;

    /// Is Recall
    /// @returns true if recall
    bool IsRecall() override;

    /// Swap Persistent delegate's EpochManager (current with transition)
    /// Happens either at Epoch Start time or after Epoch Transtion Start time
    /// if received PostCommit with E#_i
    void TransitionPersistent();

    /// Transition Retiring delegate to ForwardOnly
    void TransitionRetiring();

    /// Halt if epoch is null
    /// @param is_null result of passed in epoch(s) evaluation expression
    /// @param where function name
    void CheckEpochNull(bool is_null, const char *where);

    /// Transition Persistent or Retiring delegate
    /// @param delegeate type
    void TransitionDelegate(EpochTransitionDelegate delegate);

    /// Create EpochManager instance
    /// @param epoch_number manager's epoch number
    /// @param config delegate's configuration
    /// @param del type of transition delegate
    /// @param con type of delegate's set connection
    /// @param eb epoch block with this epoch delegates
    std::shared_ptr<EpochManager>
    CreateEpochManager(uint epoch_number, const ConsensusManagerConfig &config,
        EpochTransitionDelegate delegate, EpochConnection connnection,
        std::shared_ptr<ApprovedEB> eb);

    /// Handle consensus message
    /// @param data serialized message
    /// @param size message size
    /// @returns true on success
    bool OnP2pConsensus(uint8_t *data, size_t size);

    /// Handle delegate address advertisement message
    /// @param data serialized message
    /// @param size message size
    /// @returns true on success
    bool OnAddressAd(uint8_t *data, size_t size);

    /// Handle tx acceptor address advertisement message
    /// @param data serialized message
    /// @param size message size
    /// @returns true on success
    bool OnAddressAdTxAcceptor(uint8_t *data, size_t size);

    /// Get epoch manager for the epoch number
    /// @param epoch_number epoch number
    /// @returns epoch manager or null
    std::shared_ptr<EpochManager> GetEpochManager(uint32_t epoch_number);

    /// Get correct epoch manager pointer to propose next micro block
    /// Requires caller to lock _transition_state mutex
    /// @return shared_ptr to correct EpochManager, or nullptr if not part of new delegate set
    const std::shared_ptr<EpochManager> GetProposerEpoch();


    static const std::chrono::seconds GARBAGE_COLLECT;

    static std::atomic<uint32_t>      _cur_epoch_number;    ///< current epoch number
    EpochPeerManager                  _peer_manager;        ///< processes accept callback
    std::mutex                        _mutex;               ///< protects access to _cur_epoch
    std::shared_ptr<EpochManager>     _cur_epoch;           ///< consensus objects
    std::shared_ptr<EpochManager>     _trans_epoch;         ///< epoch transition consensus objects
    Service &                         _service;             ///< boost service
    Store &                           _store;               ///< block store reference
    Alarm &                           _alarm;               ///< alarm reference
    const Config &                    _config;              ///< consensus configuration reference
    Log                               _log;                 ///< boost log
    Archiver &                        _archiver;            ///< archiver (epoch/microblock) handler
    DelegateIdentityManager &         _identity_manager;    ///< identity manager reference
    std::atomic<EpochTransitionState> _transition_state;    ///< transition state
    EpochTransitionDelegate           _transition_delegate; ///< type of delegate during transition
    BindingMap                        _binding_map;         ///< map for binding connection to epoch manager
    static bool                       _validate_sig_config; ///< validate sig in BBS for added security
    ContainerP2p                      _p2p;                 ///< p2p-related data
    umap<ConsensusType, Timer>        _timers;
    umap<ConsensusType, bool>         _timer_set;
    umap<ConsensusType, bool>         _timer_cancelled;
    umap<ConsensusType, std::mutex>   _timer_mutexes;
};
