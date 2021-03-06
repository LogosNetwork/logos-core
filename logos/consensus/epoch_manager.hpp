/// @file
/// This file contains declaration of the EpochManager class, which encapsulates
/// consensus related types and participates in the epoch transition

#pragma once
#include <logos/epoch/epoch_transition.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/consensus/request/request_consensus_manager.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>
#include <logos/network/consensus_netio_manager.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/lib/utility.hpp>

class Archiver;
class NewEpochEventHandler;
class ConsensusScheduler;
namespace logos { class alarm; };

class EpochInfo
{

public:
    EpochInfo() = default;
    virtual ~EpochInfo() = default;
    virtual uint32_t GetEpochNumber() = 0;
    virtual EpochConnection GetConnection() = 0;
    virtual std::string GetConnectionName() = 0;
    virtual std::string GetDelegateName() = 0;
    virtual std::string GetStateName() = 0;
    virtual bool IsWaitingDisconnect() = 0;
    virtual uint8_t GetDelegateId() = 0;
    virtual DelegateIdentityManager & GetIdentityManager() = 0;
    virtual uint8_t GetNumDelegates() = 0;
};

class EpochEventsNotifier
{

public:
    EpochEventsNotifier() = default;
    virtual ~EpochEventsNotifier() = default;
    virtual void OnPostCommit(uint32_t epoch_number) = 0;
    virtual void OnPrePrepareRejected() = 0;
    virtual EpochConnection GetConnection() = 0;
    virtual uint32_t GetEpochNumber() = 0;
    virtual EpochTransitionState GetState() = 0;
    virtual EpochTransitionDelegate GetDelegate() = 0;
    virtual bool IsRecall() = 0;
};

class EpochManager : public EpochInfo,
                     public EpochEventsNotifier,
                     public Self<EpochManager>
{

    friend class ConsensusContainer;

    using Service    = boost::asio::io_service;
    using Config     = ConsensusManagerConfig;
    using Log        = boost::log::sources::logger_mt;
    using Store      = logos::block_store;
    using Cache      = logos::IBlockCache;
    using Alarm      = logos::alarm;
    template<typename T>
    using SPTR       = std::shared_ptr<T>;

public:

    EpochManager(Service & service,
                 Store & store,
                 Cache & block_cache,
                 Alarm & alarm,
                 const Config & config,
                 Archiver & archiver,
                 std::atomic<EpochTransitionState> & state,
                 std::atomic<EpochTransitionDelegate> & delegate,
                 EpochConnection connection,
                 const uint32_t epoch_number,
                 ConsensusScheduler & scheduler,
                 NewEpochEventHandler & event_handler,
                 p2p_interface & p2p,
                 uint8_t delegate_id,
                 PeerAcceptorStarter & starter,
                 const std::shared_ptr<ApprovedEB> & eb);

    ~EpochManager() final;

    uint32_t GetEpochNumber() override { return _epoch_number; }

    EpochConnection GetConnection() override { return _connection_state; }

    std::string GetConnectionName() override { return TransitionConnectionToName(_connection_state); }

    std::string GetDelegateName() override { return TransitionDelegateToName(_delegate); }

    std::string GetStateName() override { return TransitionStateToName(_state); }

    bool IsWaitingDisconnect() override { return _connection_state == EpochConnection::WaitingDisconnect; }

    EpochTransitionState GetState() override { return _state; }

    EpochTransitionDelegate GetDelegate() override { return _delegate; }

    /// Transition if received PostCommit with E#_i
    void OnPostCommit(uint32_t epoch_number) override;

    /// Transition Retiring delegate to ForwardOnly
    void OnPrePrepareRejected() override;

    bool IsRecall() override;

    void CleanUp();

    uint8_t GetDelegateId() override
    {
        return _delegate_id;
    }

    void Start();

    DelegateIdentityManager & GetIdentityManager() override;

    uint8_t GetNumDelegates() override;

private:

    std::atomic<EpochTransitionState> &     _state;             ///< State of transition, synchronized with ConsensusContainer
    std::atomic<EpochTransitionDelegate> &  _delegate;          ///< Type of transition delegate, synchronized with ConsensusContainer
    std::atomic<EpochConnection>            _connection_state;  ///< Delegate's connection set
    const uint                              _epoch_number;      ///< Epoch's number
    NewEpochEventHandler &                  _new_epoch_handler; ///< Call back on new epoch events
    //DelegateKeyStore                        _key_store; 		///< Store delegates public keys
    MessageValidator                        _validator; 		///< Validator/Signer of consensus messages
    SPTR<RequestConsensusManager>           _request_manager; 	///< Handles batch block consensus
    SPTR<MicroBlockConsensusManager>        _micro_manager; 	///< Handles micro block consensus
    SPTR<EpochConsensusManager>             _epoch_manager; 	///< Handles epoch consensus
    SPTR<ConsensusNetIOManager>             _netio_manager; 	///< Establishes connections to other delegates
    Log                                     _log;               ///< Boost log
    uint8_t                                 _delegate_id;       ///< Delegate id
    uint8_t                                 _num_delegates;
};
