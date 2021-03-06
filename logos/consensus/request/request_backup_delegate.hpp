///
/// @file
/// This file contains declaration of the RequestBackupDelegate class
/// which handles specifics of Request consensus
///
#pragma once

#include <logos/consensus/backup_delegate.hpp>

#include <unordered_set>

template<ConsensusType CT> class PersistenceManager;

class RequestBackupDelegate : public BackupDelegate<ConsensusType::Request>
{
    static constexpr ConsensusType R = ConsensusType::Request;

    using Service    = boost::asio::io_service;
    using Connection = BackupDelegate<R>;
    using Timer      = boost::asio::deadline_timer;
    using Seconds    = boost::posix_time::seconds;
    using Error      = boost::system::error_code;
    using Hashes     = std::unordered_set<BlockHash>;
    using Request    = DelegateMessage<R>;

public:

    /// Class constructor
    /// @param iochannel NetIO channel [in]
    /// @param primary pointer to PrimaryDelegate class [in]
    /// @param store reference to block store
    /// @param persistence_manager reference to PersistenceManager [in]
    /// @param key_store Delegates' public key store [in]
    /// @param validator Validator/Signer of consensus message [in]
    /// @param ids remote/local delegate id [in]
    /// @param events_notifier epoch transition helper [in]
    RequestBackupDelegate(std::shared_ptr<IOChannel> iochannel,
                          std::shared_ptr<PrimaryDelegate> primary,
                          Store & store,
                          Cache & block_cache,
                          MessageValidator & validator,
                          const DelegateIdentities & ids,
						  Service & service,
                          ConsensusScheduler & scheduler,
                          std::shared_ptr<EpochEventsNotifier> events_notifier,
                          PersistenceManager<R> & persistence_manager,
                          p2p_interface & p2p);
    ~RequestBackupDelegate() {}

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool DoValidate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message PrePrepare message [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const ApprovedRB &, uint8_t delegate_id) override;

    void DoUpdateMessage(Rejection & message);

private:

    static constexpr uint8_t TIMEOUT_MIN         = 20;
    static constexpr uint8_t TIMEOUT_RANGE       = 40;
    static constexpr uint8_t TIMEOUT_MIN_EPOCH   = 10;
    static constexpr uint8_t TIMEOUT_RANGE_EPOCH = 20;

    MessageHandler<R> & GetHandler() override { return _handler; }

    bool ValidateSequence(const PrePrepare & message);
    bool ValidateRequests(const PrePrepare & message);

    void HandlePrePrepare(const PrePrepare & message) override;
    void AdvanceCounter() override;

    void Reject(const BlockHash &) override;
    void ResetRejectionStatus() override;
    void HandleReject(const PrePrepare&) override;

    bool IsSubset(const PrePrepare & message);

    bool ValidateReProposal(const PrePrepare & message) override;

    Seconds GetTimeout(uint8_t min, uint8_t range);

    RequestMessageHandler & _handler;///< Queue of requests/proposals.
    Hashes       _pre_prepare_hashes;
    RejectionMap _rejection_map;     ///< Sets a bit for each rejected request from the PrePrepare.
};
