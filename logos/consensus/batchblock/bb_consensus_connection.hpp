///
/// @file
/// This file contains declaration of the BatchBlockConsensusConnection class
/// which handles specifics of BatchStateBlock consensus
///
#pragma once

#include <logos/consensus/consensus_connection.hpp>

class BBConsensusConnection : public ConsensusConnection<ConsensusType::BatchStateBlock>
{

    using Connection = ConsensusConnection<ConsensusType::BatchStateBlock>;

public:
    /// Class constructor
    /// @param iochannel NetIO channel [in]
    /// @param primary pointer to PrimaryDelegate class [in]
    /// @param promoter secondary list request promoter
    /// @param persistence_manager reference to PersistenceManager [in]
    /// @param key_store Delegates' public key store [in]
    /// @param validator Validator/Signer of consensus message [in]
    /// @param ids remote/local delegate id [in]
    /// @param events_notifier epoch transition helper [in]
    BBConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                                  PrimaryDelegate & primary,
                                  RequestPromoter<ConsensusType::BatchStateBlock> & promoter,
                                  PersistenceManager & persistence_manager,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids,
                                  EpochEventsNotifier & events_notifier);
    ~BBConsensusConnection() = default;

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
    PersistenceManager &        _persistence_manager; ///< PersistenceManager reference
};
