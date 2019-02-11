///
/// @file
/// This file contains declaration of the EpochBackupDelegate class
/// which handles specifics of Epoch consensus
///

#pragma once

#include <logos/consensus/backup_delegate.hpp>

class EpochBackupDelegate :
        public BackupDelegate<ConsensusType::Epoch>
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
    EpochBackupDelegate(std::shared_ptr<IOChannel> iochannel,
                             PrimaryDelegate & primary,
                             RequestPromoter<ECT> & promoter,
                             MessageValidator & validator,
                             const DelegateIdentities & ids,
                             EpochEventsNotifier & events_notifier,
                             PersistenceManager<ECT> & persistence_manager);
    ~EpochBackupDelegate() = default;

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool DoValidate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message PrePrepare message [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const ApprovedEB &, uint8_t delegate_id) override;

    bool IsPrePrepared(const BlockHash & hash) override;

private:
};
