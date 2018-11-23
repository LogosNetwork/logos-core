///
/// @file
/// This file contains declaration of the BatchBlockConsensusConnection class
/// which handles specifics of BatchStateBlock consensus
///
#pragma once

#include <logos/consensus/consensus_connection.hpp>

#include <unordered_set>


template<ConsensusType CT> class PersistenceManager;

class BBConsensusConnection : public ConsensusConnection<ConsensusType::BatchStateBlock>
{
    static constexpr ConsensusType BSBCT = ConsensusType::BatchStateBlock;

    using Service    = boost::asio::io_service;
    using Connection = ConsensusConnection<BSBCT>;
    using Promoter   = RequestPromoter<BSBCT>;
    using Timer      = boost::asio::deadline_timer;
    using Seconds    = boost::posix_time::seconds;
    using Error      = boost::system::error_code;
    using Hashes     = std::unordered_set<BlockHash>;
    using Request    = RequestMessage<BSBCT>;

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
                          Promoter & promoter,
                          MessageValidator & validator,
                          const DelegateIdentities & ids,
						  Service & service,
                          EpochEventsNotifier & events_notifier,
			  PersistenceManager<BSBCT> & persistence_manager,
			  p2p_interface & p2p);
    ~BBConsensusConnection() {}

    /// Validate PrePrepare message
    /// @param messasge PrePrepare message [in]
    /// @returns true on success
    bool DoValidate(const PrePrepare & message) override;

    /// Commit PrePrepare message to the database
    /// @param message PrePrepare message [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) override;

    bool IsPrePrepared(const logos::block_hash & hash) override;

    void DoUpdateMessage(Rejection & message);

private:

    static constexpr uint8_t TIMEOUT_MIN   = 20;
    static constexpr uint8_t TIMEOUT_RANGE = 40;
    static constexpr uint8_t TIMEOUT_MIN_EPOCH   = 10;
    static constexpr uint8_t TIMEOUT_RANGE_EPOCH = 20;

    bool ValidateSequence(const PrePrepare & message);
    bool ValidateRequests(const PrePrepare & message);

    void HandlePrePrepare(const PrePrepare & message) override;
    void OnPostCommit() override;

    void OnPrePrepareTimeout(const Error & error);

    void Reject() override;
    void ResetRejectionStatus() override;
    void HandleReject(const PrePrepare&);

    void ScheduleTimer(Seconds timeout);

    bool IsSubset(const PrePrepare & message);

    bool ValidateReProposal(const PrePrepare & message) override;

    Seconds GetTimeout(uint8_t min, uint8_t range);


    Hashes               _pre_prepare_hashes;
    Timer                _timer;
    RejectionMap         _rejection_map;       ///< Sets a bit for each rejected request from the PrePrepare.
    std::mutex           _timer_mutex;
    bool                 _cancel_timer       = false;
    bool                 _callback_scheduled = false;
};
