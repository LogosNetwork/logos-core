///
/// @file
/// This file contains definition of the MicroBlockConsensusConnection class
/// which handles specifics of MicroBlock consensus
///
#include <logos/consensus/microblock/microblock_consensus_connection.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>
#include <logos/lib/trace.hpp>

MicroBlockConsensusConnection::MicroBlockConsensusConnection(
                                  std::shared_ptr<IOChannel> iochannel,
                                  PrimaryDelegate & primary,
                                  RequestPromoter<MBCT> & promoter,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids,
                                  ArchiverMicroBlockHandler & handler,
                                  EpochEventsNotifier & events_notifier,
                                  PersistenceManager<MBCT> & persistence_manager)
    : ConsensusConnection<MBCT>(iochannel, primary, promoter, validator, ids,
                                                     events_notifier, persistence_manager)
    , _microblock_handler(handler)
{
    if (promoter.GetStore().micro_block_tip_get(_prev_pre_prepare_hash))
    {
        LOG_FATAL(_log) << "Failed to get microblock's previous hash";
        trace_and_halt();
    }
}

bool
MicroBlockConsensusConnection::DoValidate(
    const PrePrepare & message)
{
    return _persistence_manager.Validate(message, _delegate_ids.remote);
}

void
MicroBlockConsensusConnection::ApplyUpdates(
    const ApprovedMB & block,
    uint8_t)
{
    _persistence_manager.ApplyUpdates(block);

    _microblock_handler.OnApplyUpdates(block);
}

bool
MicroBlockConsensusConnection::IsPrePrepared(
    const BlockHash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return (_pre_prepare && hash == _pre_prepare->Hash());
}
