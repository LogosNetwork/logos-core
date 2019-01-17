/// @file
/// This file contains declaration of the EpochManager class, which encapsulates
/// consensus related types and participates in the epoch transition

#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/node/node.hpp>

EpochManager::EpochManager(Service & service,
                           Store & store,
                           Alarm & alarm,
                           const Config & config,
                           Archiver & archiver,
                           PeerAcceptorStarter & starter,
                           std::atomic<EpochTransitionState> & state,
                           EpochTransitionDelegate delegate,
                           EpochConnection connection,
                           const uint32_t epoch_number,
                           NewEpochEventHandler & handler)
    : _state(state)
    , _delegate(delegate)
    , _connection_state(connection)
    , _epoch_number(epoch_number)
    , _new_epoch_handler(handler)
    , _validator(_key_store, logos::genesis_delegates[DelegateIdentityManager::_global_delegate_idx].bls_key)
    , _batch_manager(service, store, config, _validator, *this)
    , _micro_manager(service, store, config, _validator, archiver, *this)
    , _epoch_manager(service, store, config, _validator, *this)
    , _netio_manager(
        {
            {ConsensusType::BatchStateBlock, _batch_manager},
            {ConsensusType::MicroBlock, _micro_manager},
            {ConsensusType::Epoch, _epoch_manager}
        },
        service, alarm, config,
        _key_store, _validator, starter, *this)
{}

EpochManager::~EpochManager()
{
    LOG_DEBUG(_log) << "EpochManager::~EpochManager";
}

void
EpochManager::OnPostCommit(
    uint32_t epoch_number)
{
    // Persistent in the new Delegate's set
    if (_delegate == EpochTransitionDelegate::Persistent &&
            _connection_state == EpochConnection::Transitioning)
    {
        _new_epoch_handler.OnPostCommit(epoch_number);
    }
}

void
EpochManager::OnPrePrepareRejected()
{
    // Retiring or Persistent in the old Delegate's set
    // received 1/3 PostCommit reject with New_Epoch error
    if (_delegate == EpochTransitionDelegate::Retiring ||
        _delegate == EpochTransitionDelegate::Persistent &&
            _connection_state == EpochConnection::Current)
    {
        _new_epoch_handler.OnPrePrepareRejected(_delegate);
    }
}

bool
EpochManager::IsRecall()
{
    return _new_epoch_handler.IsRecall();
}

void
EpochManager::CleanUp()
{
    _netio_manager.CleanUp();
}