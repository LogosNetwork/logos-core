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
                                  std::shared_ptr<PrimaryDelegate> primary,
                                  Store & store,
                                  Cache & block_cache,
                                  MessageValidator & validator,
                                  const DelegateIdentities & ids,
                                  ArchiverMicroBlockHandler & handler,
                                  ConsensusScheduler & scheduler,
                                  std::shared_ptr<EpochEventsNotifier> events_notifier,
                                  PersistenceManager<MBCT> & persistence_manager,
                                  p2p_interface & p2p,
                                  Service & service);
    ~MicroBlockBackupDelegate() = default;

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool DoValidate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message ApprovedMB [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const ApprovedMB &, uint8_t delegate_id) override;


protected:
    bool ValidateTimestamp(const PrePrepare & message) override;

    /// set previous hash, microblock and epoch block have only one chain
    /// consequently primary has to set all backup's hash to previous
    /// @param hash to set
    void SetPreviousPrePrepareHash(const BlockHash &hash) override
    {
        auto primary = GetSharedPtr(_primary, "MicroBlockBackupDelegate::SetPreviousPrepareHash, object destroyed");
        if (primary)
        {
            primary->SetPreviousPrePrepareHash(hash);
        }
    }

private:
    void AdvanceCounter() override;

    MessageHandler<MBCT> & GetHandler() override { return _handler; }
    MicroBlockMessageHandler &   _handler;
    ArchiverMicroBlockHandler &  _microblock_handler;
};
