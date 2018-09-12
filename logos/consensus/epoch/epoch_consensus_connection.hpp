///
/// @file
/// This file contains declaration of the EpochConsensusConnection class
/// which handles specifics of Epoch consensus
///

#pragma once

#include <logos/consensus/consensus_connection.hpp>

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
    EpochConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
                             PrimaryDelegate * primary,
                             DelegateKeyStore & key_store,
                             MessageValidator & validator,
                             const DelegateIdentities & ids)
        : ConsensusConnection<ConsensusType::Epoch>(iochannel, primary, key_store, validator, ids)
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
};
