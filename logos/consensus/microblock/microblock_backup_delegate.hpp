///
/// @file
/// This file contains declaration of the MicroBlockBackupDelegate class
/// which handles specifics of MicroBlock consensus
///
#pragma once

#include <logos/consensus/backup_delegate.hpp>


class ArchiverMicroBlockHandler;

class MicroBlockBackupDelegate :
        public BackupDelegate<ConsensusType::MicroBlock>
{
    static constexpr ConsensusType MBCT = ConsensusType::MicroBlock;
public:
    /// Class constructor
    /// @param iochannel NetIO channel [in]
    /// @param primary pointer to PrimaryDelegate class [in]
    /// @param key_store Delegates' public key store [in]
    /// @param validator Validator/Signer of consensus message [in]
    /// @param ids remote/local delegate id [in]
    /// @param events_notifier epoch transition helper [in]
    MicroBlockBackupDelegate(std::shared_ptr<IOChannel> iochannel,
                                  PrimaryDelegate & primary,
                                  RequestPromoter<MBCT> & promoter,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids,
                                  ArchiverMicroBlockHandler & handler,
                                  EpochEventsNotifier & events_notifier,
                                  PersistenceManager<MBCT> & persistence_manager);
    ~MicroBlockBackupDelegate() = default;

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool DoValidate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message ApprovedMB [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const ApprovedMB &, uint8_t delegate_id) override;

    bool IsPrePrepared(const BlockHash & hash) override;

protected:
    bool ValidateTimestamp(const PrePrepare & message) override;

    /// set previous hash, microblock and epoch block have only one chain
    /// consequently primary has to set all backup's hash to previous
    /// @param hash to set
    void SetPreviousPrePrepareHash(const BlockHash &hash) override
    {
        _primary.SetPreviousPrePrepareHash(hash);
    }

private:
    ArchiverMicroBlockHandler &  _microblock_handler;
};
