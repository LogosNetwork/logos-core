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
    static constexpr ConsensusType ECT = ConsensusType::Epoch;
public:
    /// Class constructor
    /// @param iochannel NetIO channel [in]
    /// @param primary pointer to PrimaryDelegate class [in]
    /// @param key_store Delegates' public key store [in]
    /// @param validator Validator/Signer of consensus message [in]
    /// @param ids remote/local delegate id [in]
    /// @param events_notifier epoch transition helper [in]
    EpochConsensusConnection(std::shared_ptr<IOChannel> iochannel,
                             PrimaryDelegate & primary,
                             RequestPromoter<ECT> & promoter,
                             MessageValidator & validator,
                             const DelegateIdentities & ids,
                             ArchiverEpochHandler & handler,
                             EpochEventsNotifier & events_notifier,
                             PersistenceManager<ECT> & persistence_manager);
    ~EpochConsensusConnection() = default;

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool DoValidate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message PrePrepare message [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) override;

    bool IsPrePrepared(const logos::block_hash & hash) override;

private:
    ArchiverEpochHandler & _epoch_handler; ///< Epoch handler
};
