/// @file
/// This file contains declaration of the EpochManager class, which encapsulates
/// consensus related types and participates in the epoch transition

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
    , _validator(_key_store)
    , _batch_manager(service, store,
                 config, _key_store, _validator, *this)
    , _micro_manager(service, store,
                 config, _key_store, _validator, archiver, *this)
    , _epoch_manager(service, store,
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
EpochManager::OnPostCommit(
    uint32_t epoch_number)
{
    if (_delegate == EpochTransitionDelegate::Persistent)
    {
        _new_epoch_handler.OnPostCommit(epoch_number);
    }
}

void
EpochManager::OnPrePrepareRejected()
{
    if (_delegate == EpochTransitionDelegate::Retiring)
    {
        _new_epoch_handler.OnPrePrepareRejected();
    }
}

bool
EpochManager::IsRecall()
{
    return _new_epoch_handler.IsRecall();
}