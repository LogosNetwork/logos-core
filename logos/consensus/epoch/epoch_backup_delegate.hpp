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
                             std::shared_ptr<PrimaryDelegate> primary,
                             Store & store,
                             MessageValidator & validator,
                             const DelegateIdentities & ids,
                             ConsensusScheduler & scheduler,
                             std::shared_ptr<EpochEventsNotifier> events_notifier,
                             PersistenceManager<ECT> & persistence_manager,
                             p2p_interface & p2p,
                             Service & service);
    ~EpochBackupDelegate() = default;

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool DoValidate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message PrePrepare message [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const ApprovedEB &, uint8_t delegate_id) override;

protected:
    bool ValidateTimestamp(const PrePrepare &message) override;

    /// set previous hash, microblock and epoch block have only one chain
    /// consequently primary has to set all backup's hash to previous
    /// @param hash to set
    void SetPreviousPrePrepareHash(const BlockHash &hash) override
    {
        auto primary = GetSharedPtr(_primary, "EpochBackupDelegate::SetPreviousPrepareHash, object destroyed");
        if (primary)
        {
            primary->SetPreviousPrePrepareHash(hash);
        }
    }

private:
    MessageHandler<ECT> & GetHandler() override { return _handler; }
    EpochMessageHandler & _handler;
};
