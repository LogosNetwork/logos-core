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
#include <logos/epoch/archiver.hpp>
#include <logos/epoch/epoch_transition.hpp>
#include <logos/epoch/event_proposer.hpp>
#include <logos/tx_acceptor/tx_channel.hpp>

#include <queue>

namespace logos
{
    class node_config;
    class state_block;
    class node;
}

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
    virtual void EpochStart() = 0;
    virtual bool IsRecall() = 0;
    virtual bool TransitionEventsStarted() = 0;
    virtual void UpcomingEpochSetUp() = 0;
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
    virtual bool CanBind(uint32_t epoch_number) = 0;
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
    friend class MicroBlockTester;

    using Service    = boost::asio::io_service;
    using Config     = logos::node_config;
    using Store      = logos::block_store;
    using Cache      = logos::IBlockCache;
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
                       Cache & block_cache,
                       logos::alarm & alarm,
                       const logos::node_config & config,
                       IRecallHandler & recall_handler,
                       DelegateIdentityManager & identity_manager,
                       p2p_interface & p2p);

    ~ConsensusContainer()
    {
        LOG_DEBUG(_log) << "ConsensusContainer::~ConsensusContainer()";
    }

    /// Start consensus container
    void Start();

    /// Activate consensus components
    /// Called either directly by node::ActivateConsensus() or IM OnSleeved()
    void ActivateConsensus();

    /// Deactivate consensus components
    void DeactivateConsensus();

    /// Checks if epoch transition events have started (but not the new epoch)
    /// i.e. the transition state is Connecting or EpochTransitionStart
    /// @return true if transition state is one of the above, and false otherwise.
    bool TransitionEventsStarted() override;

    /// Perform setup operations for a delegate to be activated in the next epoch.
    /// If the delegate is in office next, perform endpoint advertisement and possibly
    /// build the upcoming EpochManager if transition events have already started
    /// (if not, it will be taken care of by event proposer); otherwise do nothing.
    /// Only called by DelegateIdentityManager::OnSleeved()
    void UpcomingEpochSetUp() override;

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

    static uint32_t QueriedEpochToNumber(QueriedEpoch q)
    {
        return (q == QueriedEpoch::Current) ? (GetCurEpochNumber() - 2) : (GetCurEpochNumber() - 1);
    }


    /// Binds connected socket to the correct delegates set, mostly applicable during epoch transition
    /// @param endpoint connected endpoing
    /// @param socket connected socket
    /// @param connection type of peer's connection
    bool Bind(std::shared_ptr<Socket>,
              const Endpoint endpoint,
              uint32_t epoch_number,
              uint8_t delegate_id) override;

    /// Returns true if binding map contains an entry for the specified
    //  epoch number
    bool CanBind(uint32_t epoch_number) override;

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

    PeerInfoProvider & GetPeerInfoProvider()
    {
        return _p2p;
    }

    /// Get epoch manager for the epoch number
    /// @param epoch_number epoch number
    /// @returns epoch manager or null
    std::shared_ptr<EpochManager> GetEpochManager(uint32_t epoch_number);

    EpochTransitionState GetTransitionState() { return _transition_state; }

    EpochTransitionDelegate GetTransitionDelegate() { return _transition_delegate; }

    uint8_t GetTransitionIdx() { return _transition_del_idx; }

protected:

    /// Initiate MicroBlock consensus, internal request
    ///        @param[in] MicroBlock containing the batch blocks
    logos::process_return OnDelegateMessage(std::shared_ptr<DelegateMessage<ConsensusType::MicroBlock>> message) override;

    /// Initiate Epoch consensus, internal request
    ///        @param[in] Epoch containing the microblocks
    logos::process_return OnDelegateMessage(std::shared_ptr<DelegateMessage<ConsensusType::Epoch>> message) override;

private:

    void LogEvent(const std::string &, const uint32_t &);

    /// Set current epoch id, this is done by the NodeIdentityManager on startup
    /// And by epoch transition logic
    /// @param id epoch id
    static void SetCurEpochNumber(uint32_t n) { _cur_epoch_number = n; }

    /// Epoch transition start event at T-20sec
    void EpochTransitionStart();

    /// Epoch start event at T(00:00)
    void EpochStart() override;

    /// Epoch start event at T+20sec
    void EpochTransitionEnd();

    void ScheduleEpochTransitionStart();

    void SetTransitionDelegate(bool, bool);

    /// Build consensus configuration
    /// @param delegate_idx delegate's index [in]
    /// @param delegates in the epoch [in]
    /// @returns delegate's configuration
    ConsensusManagerConfig BuildConsensusConfig(uint8_t delegate_idx, const ApprovedEB &epoch);

    void BuildUpcomingEpochManager(const uint8_t &, const std::shared_ptr<ApprovedEB> &);

    /// Is Recall
    /// @returns true if recall
    bool IsRecall() override;

    /// Halt if epoch is null
    /// @param is_null result of passed in epoch(s) evaluation expression
    /// @param where function name
    void CheckEpochNull(bool is_null, const char *where);

    /// Get the epoch manager currently active for consensus proposal, if any exists.
    ///
    /// This function is used to 1) detect if consensus is currently active,
    /// and 2) retrieve the actual EpochManager and initiate consensus.
    /// Note that for archival blocks during epoch transition, the responsible proposer is
    /// always the new epoch manager if the delegate is Persistent.
    /// Caller function needs to lock _mutex!
    /// @param[in] boolean flag to indicate if we need
    /// @return
    std::shared_ptr<EpochManager> GetProposerEpochManager(bool archival = false);

    /// Transition Persistent or Retiring delegate
    /// This method should only be called by EpochStart()
    void TransitionDelegate();

    /// Create EpochManager instance
    /// @param epoch_number manager's epoch number
    /// @param config delegate's configuration
    /// @param con type of delegate's set connection
    /// @param eb const reference to epoch block pointer with this epoch's delegates
    std::shared_ptr<EpochManager>
    CreateEpochManager(uint epoch_number, const ConsensusManagerConfig &config,
        EpochConnection connnection, const std::shared_ptr<ApprovedEB> & eb);

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

    static std::atomic<uint32_t>         _cur_epoch_number;    ///< current epoch number
    EpochPeerManager                     _peer_manager;        ///< processes accept callback
    EventProposer                        _event_proposer;      ///< schedules epoch transition events (and optionally archival)
    Archiver                             _archiver;            ///< archiver (epoch/microblock) handler
    std::mutex                           _mutex;               ///< protects access to binding map, transition state and transition delegate
    Service &                            _service;             ///< boost service
    Store &                              _store;               ///< block store reference
    Cache &                              _block_cache;         ///< block cache reference
    Alarm &                              _alarm;               ///< alarm reference
    const Config &                       _config;              ///< consensus configuration reference
    Log                                  _log;                 ///< boost log
    DelegateIdentityManager &            _identity_manager;    ///< identity manager reference
    std::atomic<EpochTransitionState>    _transition_state;    ///< transition state, synchronized with EpochManager(s)
    std::atomic<EpochTransitionDelegate> _transition_delegate; ///< type of delegate during transition, synchronized with EpochManager(s)
    uint8_t                              _transition_del_idx;  ///< index of new delegate during transition, if applicable
    BindingMap                           _binding_map;         ///< map for binding connection to epoch manager
    static bool                          _validate_sig_config; ///< validate sig in BBS for added security
    ContainerP2p                         _p2p;                 ///< p2p-related data
    umap<ConsensusType, Timer>           _timers;
    umap<ConsensusType, bool>            _timer_set;
    umap<ConsensusType, bool>            _timer_cancelled;
    umap<ConsensusType, std::mutex>      _timer_mutexes;
};
