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
                                  RequestPromoter<ConsensusType::MicroBlock> & promoter,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids,
                                  ArchiverMicroBlockHandler & handler,
				  EpochEventsNotifier & events_notifier,
				  p2p_interface & p2p)
    : ConsensusConnection<ConsensusType::MicroBlock>(iochannel, primary, promoter, validator, ids,
						     events_notifier, p2p)
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
    return _microblock_handler.Validate(message);
}

void
MicroBlockConsensusConnection::ApplyUpdates(
    const PrePrepare & block,
    uint8_t delegate_id)
{
    _microblock_handler.CommitToDatabase(block);
}

bool
MicroBlockConsensusConnection::IsPrePrepared(
    const logos::block_hash & hash)
{
    std::lock_guard<std::mutex> lock(_mutex);

    return (_pre_prepare && hash == _pre_prepare->hash());
}
