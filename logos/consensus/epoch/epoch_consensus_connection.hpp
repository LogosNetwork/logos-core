///
/// @file
/// This file contains declaration of the EpochConsensusConnection class
/// which handles specifics of Epoch consensus
///

#pragma once

#include <logos/consensus/consensus_connection.hpp>

class IArchiverEpochHandler;

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
                             PrimaryDelegate * primary,
                             MessageValidator & validator,
                             const DelegateIdentities & ids,
                             IArchiverEpochHandler & handler)
        : ConsensusConnection<ConsensusType::Epoch>(iochannel, primary, validator, ids)
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

private:
    IArchiverEpochHandler & _epoch_handler; ///< Epoch handler
};
