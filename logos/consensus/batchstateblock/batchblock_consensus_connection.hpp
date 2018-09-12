///
/// @file
/// This file contains declaration of the BatchBlockConsensusConnection class
/// which handles specifics of BatchStateBlock consensus
///
#pragma once

#include <logos/consensus/consensus_connection.hpp>

class BatchBlockConsensusConnection :
        public ConsensusConnection<ConsensusType::BatchStateBlock>
{
public:
    /// Class constructor
    /// @param iochannel NetIO channel [in]
    /// @param primary pointer to PrimaryDelegate class [in]
    /// @param persistence_manager reference to PersistenceManager [in]
    /// @param key_store Delegates' public key store [in]
    /// @param validator Validator/Signer of consensus message [in]
    /// @param ids remote/local delegate id [in]
    BatchBlockConsensusConnection(std::shared_ptr<IIOChannel> iochannel,
                                  PrimaryDelegate * primary,
                                  PersistenceManager & persistence_manager,
                                  DelegateKeyStore & key_store,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids)
        : ConsensusConnection<ConsensusType::BatchStateBlock>(iochannel, primary, key_store, validator,ids)
        , _persistence_manager(persistence_manager)
    {}
    ~BatchBlockConsensusConnection() {}

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool Validate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message PrePrepare message [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) override;

private:
    PersistenceManager &        _persistence_manager; ///< PersistenceManager reference
};
