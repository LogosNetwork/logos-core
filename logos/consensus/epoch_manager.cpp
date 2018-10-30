/// @file
/// This file contains declaration of the EpochManager class, which encapsulates
/// consensus related types and participates in the epoch transition

#include <logos/consensus/epoch_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/node/node.hpp>

EpochManager::EpochManager(Service & service,
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
                           std::function<void()> on_new_epoch_postcommit,
                           std::function<void()> on_new_epoch_reject)
    : _state(state)
    , _delegate(delegate)
    , _connection_state(connection)
    , _epoch_number(epoch_number)
    , _on_new_epoch_postcommit(on_new_epoch_postcommit)
    , _on_new_epoch_reject(on_new_epoch_reject)
    , _validator(_key_store)
    , _batch_manager(service, store, log,
                 config, _key_store, _validator, *this)
    , _micro_manager(service, store, log,
                 config, _key_store, _validator, archiver, *this)
    , _epoch_manager(service, store, log,
                 config, _key_store, _validator, archiver, *this)
    , _netio_manager(
        {
                {ConsensusType::BatchStateBlock, _batch_manager},
                {ConsensusType::MicroBlock, _micro_manager},
                {ConsensusType::Epoch, _epoch_manager}
        },
        service, alarm, config,
        _key_store, _validator, starter, *this)
{}

void
EpochManager::OnNewEpochPostCommit(
    uint epoch_number)
{
    if (_delegate == EpochTransitionDelegate::Persistent &&
            (_epoch_number + 1) == epoch_number)
    {
        _on_new_epoch_postcommit();
    }
}

void
EpochManager::OnNewEpochReject()
{}