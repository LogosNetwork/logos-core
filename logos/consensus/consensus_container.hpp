/// @file
/// This file contains the declaration of the ConsensusContainer class, which encapsulates
/// consensus related types
#pragma once

#include <logos/consensus/batchblock/batchblock_consensus_manager.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/consensus/network/consensus_netio_manager.hpp>
#include <logos/consensus/network/epoch_peer_manager.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/node/node_identity_manager.hpp>
#include <logos/epoch/epoch_transition.hpp>

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
    bool _locked;
    std::mutex & _mutex;
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

};

class InternalConsensus
{
public:
    InternalConsensus() = default;
    virtual ~InternalConsensus() = default;
    virtual logos::process_return OnSendRequest(std::shared_ptr<MicroBlock>) = 0;
    virtual logos::process_return OnSendRequest(std::shared_ptr<Epoch>) = 0;
    virtual void EpochTransitionEventsStart() = 0;
};

/// Encapsulates consensus related objects.
///
/// This class serves as a container for ConsensusManagers
/// and other consensus-related types and provides an interface
/// to the node object.
class ConsensusContainer : public InternalConsensus
{
    friend class NodeIdentityManager;

    using Service    = boost::asio::io_service;
    using Config     = ConsensusManagerConfig;
    using Log        = boost::log::sources::logger_mt;
    using Store      = logos::block_store;
    using Alarm      = logos::alarm;
    using Endpoint   = boost::asio::ip::tcp::endpoint;
    using Socket     = boost::asio::ip::tcp::socket;
    using Accounts   = logos::account[NUM_DELEGATES];
    using BindingMap = std::map<EpochConnection, std::shared_ptr<EpochManager>>;

    struct ConnectionCache {
        std::shared_ptr<Socket> socket;
        std::shared_ptr<KeyAdvertisement> advert;
        Endpoint endpoint;
    };

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
                       Log & log,
                       const Config & config,
                       Archiver & archiver,
                       NodeIdentityManager & identity_manager);

    ~ConsensusContainer() = default;

    /// Handles requests for batch block consensus.
    ///
    /// Submits transactions to consensus logic.
    ///     @param[in] block state_block containing the transaction
    ///     @param[in] should_buffer bool flag that, when set, will
    ///                              cause the block to be buffered
    ///     @return process_return result of the operation
    logos::process_return OnSendRequest(std::shared_ptr<logos::state_block> block,
                                        bool should_buffer);

    /// Called when buffering is done for batch block consensus.
    ///
    /// Indicates that the buffering of state blocks is complete.
    ///     @param[out] result result of the operation
    void BufferComplete(logos::process_return & result);

    /// Get current epoch id
    /// @returns current epoch id
    static uint GetCurEpochNumber() { return _cur_epoch_number; }

    /// Binds connected socket to the correct delegates set, mostly applicable during epoch transition
    /// @param endpoint connected endpoing
    /// @param socket connected socket
    /// @param advert key advertisement received from the client
    void PeerBinder(const Endpoint, std::shared_ptr<Socket>, std::shared_ptr<KeyAdvertisement>);

    /// Start Epoch Transition
    void EpochTransitionEventsStart() override;

protected:

	/// Initiate MicroBlock consensus
	///		@param[in] MicroBlock containing the batch blocks
    logos::process_return OnSendRequest(std::shared_ptr<MicroBlock>) override;

    /// Initiate MicroBlock consensus
    ///		@param[in] Epoch containing the microblocks
    logos::process_return OnSendRequest(std::shared_ptr<Epoch>) override;

private:
    /// Set current epoch id, this is done by the NodeIdentityManager on startup
    /// And by epoch transition logic
    /// @param id epoch id
    static void SetCurEpochNumber(uint n) { _cur_epoch_number = n; }

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
    Config BuildConsensusConfig(uint8_t delegate_idx, const Accounts & delegates);

    /// Submit connections queue for binding to the correct epoch
    void BindConnectionsQueue();

    /// Swap Persistent delegate's EpochManager (current with transition)
    /// Happens either at Epoch Start time or after Epoch Transtion Start time
    /// if received PostCommit with E#_i
    /// @return true if no error
    bool OnNewEpochPostCommit();

    /// Transition Retiring delegate to ForwardOnly
    /// @return true if no error
    bool OnNewEpochReject();

    /// Create EpochManager instance
    /// @param epoch_number manager's epoch number
    /// @param config delegate's configuration
    /// @param del type of transition delegate
    /// @param con type of delegate's set connection
    std::shared_ptr<EpochManager>
    CreateEpochManager(uint epoch_number, const ConsensusManagerConfig &config,
        EpochTransitionDelegate delegate, EpochConnection connnection);

    static std::atomic_uint             _cur_epoch_number;          ///< current epoch number
    EpochPeerManager                    _peer_manager;              ///< processes accept callback
    std::mutex                          _mutex;                     ///< protects access to _cur_epoch
    std::shared_ptr<EpochManager>       _cur_epoch;                 ///< consensus objects
    std::shared_ptr<EpochManager>       _trans_epoch;               ///< epoch transition consensus objects
    Service &                           _service;                   ///< boost service
    Store &                             _store;                     ///< block store reference
    Alarm &                             _alarm;                     ///< alarm reference
    const Config &                      _config;                    ///< consensus configuration reference
    Log &                               _log;                       ///< boost log
    Archiver &                          _archiver;                  ///< archiver (epoch/microblock) handler
    NodeIdentityManager &               _identity_manager;          ///< identity manager reference
    std::atomic<EpochTransitionState>   _transition_state;          ///< transition state
    EpochTransitionDelegate             _transition_delegate;       ///< type of delegate during transition
    std::queue<ConnectionCache>         _connections_queue;         ///< queue for delegates set connections
    BindingMap                          _binding_map;               ///< map for binding connection to epoch manager
};
