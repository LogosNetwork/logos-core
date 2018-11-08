///
/// @file
/// This file contains declaration of the EpochConsensusConnection class
/// which handles specifics of Epoch consensus
///
#include <logos/consensus/epoch/epoch_consensus_connection.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/epoch/archiver.hpp>

EpochConsensusConnection::EpochConsensusConnection(
                             std::shared_ptr<IOChannel> iochannel,
                             PrimaryDelegate & primary,
                             RequestPromoter<ConsensusType::Epoch> & promoter,
                             MessageValidator & validator,
                             const DelegateIdentities & ids,
                             ArchiverEpochHandler & handler,
                             EpochEventsNotifier & events_notifier)
    : ConsensusConnection<ConsensusType::Epoch>(iochannel, primary, promoter, validator,
                                                ids, events_notifier)
    , _epoch_handler(handler)
{
    if (promoter.GetStore().epoch_tip_get(_prev_pre_prepare_hash))
    {
        LOG_ERROR(_log) << "Failed to get eporh's previous hash";
    }
}

bool
EpochConsensusConnection::DoValidate(
    const PrePrepare & message)
{
    return _epoch_handler.Validate(message);
}

void
EpochConsensusConnection::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    _epoch_handler.CommitToDatabase(block);
}

bool
EpochConsensusConnection::IsPrePrepared(
    const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return (_pre_prepare && hash == _pre_prepare->hash());
}
