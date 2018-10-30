/// @file
/// This file contains declaration of the EpochManager class, which encapsulates
/// consensus related types and participates in the epoch transition

#pragma once
#include <logos/epoch/epoch_transition.hpp>
#include <logos/consensus/batchblock/batchblock_consensus_manager.hpp>
#include <logos/consensus/microblock/microblock_consensus_manager.hpp>
#include <logos/consensus/network/consensus_netio_manager.hpp>
#include <logos/consensus/epoch/epoch_consensus_manager.hpp>

class Archiver;
namespace logos { class alarm; };

class EpochInfo {
public:
    EpochInfo() = default;
    virtual ~EpochInfo() = default;
    virtual uint GetEpochNumber() = 0;
    virtual EpochConnection GetConnection() = 0;
    virtual std::string GetConnectionName() = 0;
    virtual std::string GetDelegateName() = 0;
    virtual std::string GetStateName() = 0;
    virtual bool IsWaitingDisconnect() = 0;
};

class EpochEventsNotifier {
public:
    EpochEventsNotifier() = default;
    virtual ~EpochEventsNotifier() = default;
    virtual void OnNewEpochPostCommit(uint epoch_number) = 0;
    virtual void OnNewEpochReject() = 0;
    virtual EpochConnection GetConnection() = 0;
    virtual uint GetEpochNumber() = 0;
};

class EpochManager : public EpochInfo,
                     public EpochEventsNotifier
{

    friend class ConsensusContainer;

    using Service    = boost::asio::io_service;
    using Config     = ConsensusManagerConfig;
    using Log        = boost::log::sources::logger_mt;
    using Store      = logos::block_store;
    using Alarm      = logos::alarm;

public:
    EpochManager(Service & service,
                     Store & store,
                     Alarm & alarm,
                     Log & log,
                     const Config & config,
                     Archiver & archiver,
                     PeerAcceptorStarter & starter,
                     std::atomic<EpochTransitionState> & state,
                     EpochTransitionDelegate delegate,
                     EpochConnection connection,
                     const uint epoch_number,
                     std::function<void()> persistent_to_reject,
                     std::function<void()> retiring_to_forward_only);
    ~EpochManager() {}

    uint GetEpochNumber() override { return _epoch_number; }

    EpochConnection GetConnection() override { return _connection_state; }

    std::string GetConnectionName() override { return TransitionConnectionToName(_connection_state); }

    std::string GetDelegateName() override { return TransitionDelegateToName(_delegate); }

    std::string GetStateName() override { return TransitionStateToName(_state); }

    bool IsWaitingDisconnect() override { return _connection_state == EpochConnection::WaitingDisconnect; }

    void OnNewEpochPostCommit(uint epoch_number) override;

    void OnNewEpochReject() override;

private:
    std::atomic<EpochTransitionState> &     _state;             ///< State of transition
    std::atomic<EpochTransitionDelegate>    _delegate;          ///< Type of transition delegate
    std::atomic<EpochConnection>            _connection_state;  ///< Delegate's connection set
    const uint                              _epoch_number;      ///< Epoch's number
    std::function<void()>                   _on_new_epoch_postcommit; ///< Call back on new epoch PostCommit
    std::function<void()>                   _on_new_epoch_reject;     ///< Call back on new epoch PrePrepare reject
    DelegateKeyStore                        _key_store; 		///< Store delegates public keys
    MessageValidator                        _validator; 		///< Validator/Signer of consensus messages
    BatchBlockConsensusManager              _batch_manager; 	///< Handles batch block consensus
    MicroBlockConsensusManager	            _micro_manager; 	///< Handles micro block consensus
    EpochConsensusManager	                _epoch_manager; 	///< Handles epoch consensus
    ConsensusNetIOManager                   _netio_manager; 	///< Establishes connections to other delegates
    Log                                     _log;               ///< Boost log reference

    /// Update secondary request handler promoter during epoch transition
    void UpdateRequestPromoter()
    {
        _batch_manager.UpdateRequestPromoter();
        _micro_manager.UpdateRequestPromoter();
        _epoch_manager.UpdateRequestPromoter();
    }
};