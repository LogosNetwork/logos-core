///
/// @file
/// This file contains declaration of the Archiver class - container for epoch/microblock handling related classes
///
#pragma once
#include <logos/microblock/microblock_handler.hpp>
#include <logos/epoch/event_proposer.hpp>
#include <logos/epoch/epoch_handler.hpp>

namespace logos
{
    class alarm;
    class block_store;
}

class ConsensusContainer;

/// Container for epoch/microblock handling related classes
class Archiver
{
    using Log           = boost::log::sources::logger_mt;
public:
    /// Class constructor
    /// @param alarm logos alarm reference [in]
    /// @param store logos block_store reference [in]
    /// @param delegate_id this delegate id [in]
    /// @param consensus_container consensus_container reference [in]
    Archiver(logos::alarm&, BlockStore&, uint8_t, ConsensusContainer &);
    ~Archiver() = default;

    /// Start archiving events
    void Start();
private:
    EventProposer       _event_proposer;
    MicroBlockHandler   _micro_block_handler;
    EpochHandler        _epoch_handler;
    ConsensusContainer &_consensus_container;
    Log                 _log;
};