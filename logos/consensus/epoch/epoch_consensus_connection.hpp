///
/// @file
/// This file contains declaration of the EpochConsensusConnection class
/// which handles specifics of Epoch consensus
///

#pragma once

#include <logos/consensus/consensus_connection.hpp>

class ArchiverEpochHandler;

class EpochConsensusConnection :
        public ConsensusConnection<ConsensusType::Epoch>
{
public:
    /// Class constructor
    /// @param iochannel NetIO channel [in]
    /// @param primary pointer to PrimaryDelegate class [in]
    /// @param key_store Delegates' public key store [in]
    /// @param validator Validator/Signer of consensus message [in]
    /// @param ids remote/local delegate id [in]
    EpochConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                             PrimaryDelegate & primary,
                             RequestPromoter<ConsensusType::Epoch> & promoter,
                             MessageValidator & validator,
                             const DelegateIdentities & ids,
			     ArchiverEpochHandler & handler,
			     p2p_interface & p2p)
	: ConsensusConnection<ConsensusType::Epoch>(iochannel, primary, promoter, validator, ids, p2p)
        , _epoch_handler(handler)
    {}
    ~EpochConsensusConnection() {}

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool Validate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message PrePrepare message [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) override;

    bool IsPrePrepared(const logos::block_hash & hash) override;

private:
    ArchiverEpochHandler & _epoch_handler; ///< Epoch handler
};
