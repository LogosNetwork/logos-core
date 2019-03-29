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
                           std::atomic<EpochTransitionState> & state,
                           EpochTransitionDelegate delegate,
                           EpochConnection connection,
                           const uint32_t epoch_number,
                           NewEpochEventHandler & handler,
                           p2p_interface & p2p,
                           uint8_t delegate_id)
    : _state(state)
    , _delegate(delegate)
    , _connection_state(connection)
    , _epoch_number(epoch_number)
    , _new_epoch_handler(handler)
    , _validator(_key_store, logos::genesis_delegates[DelegateIdentityManager::_global_delegate_idx].bls_key)
    , _request_manager(std::make_shared<RequestConsensusManager>(service, store, config, _validator, p2p, epoch_number))
    , _micro_manager(std::make_shared<MicroBlockConsensusManager>(service, store, config, _validator, archiver, p2p, epoch_number))
    , _epoch_manager(std::make_shared<EpochConsensusManager>(service, store, config, _validator, p2p, epoch_number))
    , _netio_manager(std::make_shared<ConsensusNetIOManager>(_request_manager, _micro_manager, _epoch_manager,
                     service, alarm, config, _key_store, _validator))
    , _delegate_id(delegate_id)
{
}

EpochManager::~EpochManager()
{
    LOG_DEBUG(_log) << "EpochManager::~EpochManager";
    if (_delegate == EpochTransitionDelegate::Retiring ||
            _delegate == EpochTransitionDelegate::RetiringForwardOnly ||
            _delegate == EpochTransitionDelegate::None)
    {
        _request_manager->ClearWaitingList();
        _micro_manager->ClearWaitingList();
        _epoch_manager->ClearWaitingList();
    }
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
    _netio_manager->CleanUp();
}

void
EpochManager::Start(PeerAcceptorStarter & starter)
{
    auto this_l = shared_from_this();
    _request_manager->Init(this_l);
    _micro_manager->Init(this_l);
    _epoch_manager->Init(this_l);
    _netio_manager->Start(this_l, starter);
}
