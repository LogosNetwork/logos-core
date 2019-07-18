/// @file
/// This file contains declaration of the EpochConsensusManager class, which
/// handles specifics of Epoch consensus
#pragma once

#include <logos/consensus/consensus_manager.hpp>

class ArchiverEpochHandler;

///< EpochConsensusManager handles specifics of Epoch consensus
class EpochConsensusManager: public ConsensusManager<ConsensusType::Epoch>
{
public:
	/// Class constructor
	///
	/// Called by ConsensusContainer.
	///     @param[in] service reference to boost asio service
	///     @param[in] store reference to blockstore
	///     @param[in] log reference to boost asio log
	///     @param[in] config reference to ConsensusManagerConfig configuration
	///     @param[in] validator validator/signer of consensus messages
	///     @param[in] events_notifier epoch transition helper
	EpochConsensusManager(Service & service,
	                      Store & store,
                          Cache & block_cache,
                          const Config & config,
						  ConsensusScheduler & scheduler,
                          MessageValidator & validator,
                          p2p_interface & p2p,
                          uint32_t epoch_number);

    ~EpochConsensusManager() = default;

    /// Handles benchmark request.
    ///
    /// Special benchmark request.
    ///     @param[in]  epoch block
    ///     @param[out] result result of the operation
    void OnBenchmarkDelegateMessage(
        std::shared_ptr<DelegateMessage> message,
        logos::process_return & result) override;

    /// set previous hash, microblock and epoch block have only one chain
    /// consequently primary has to set all backup's hash to previous
    /// @param hash to set
    void SetPreviousPrePrepareHash(const BlockHash &hash) override
    {
        std::lock_guard<std::mutex> lock(_connection_mutex);
        for (auto conn : _connections)
        {
            conn->BackupDelegate::SetPreviousPrePrepareHash(hash);
        }
        PrimaryDelegate::SetPreviousPrePrepareHash(hash);
    }

protected:
    /// Commit to the store.
    ///
    /// Commits the block to the database.
    ///     @param[in] block the epoch block to commit to the database
    ///     @param[in] delegate_id delegate id
    void ApplyUpdates(const ApprovedEB &,
        uint8_t delegate_id) override;

    /// Returns number of stored blocks.
    ///
    /// Benchmarking related.
    ///     @return number of stored blocks
    uint64_t GetStoredCount() override;

    /// Validates epoch block.
    ///     @param[in]  block the block to be validated
    ///     @param[out] result of the validation
    ///     @return true if validated false otherwise
    bool Validate(
        std::shared_ptr<DelegateMessage> block,
        logos::process_return & result) override;

    /// Gets next available epoch block
	///		@return reference to epoch block
    PrePrepare & PrePrepareGetNext(bool) override;

    PrePrepare & PrePrepareGetCurr() override;

    /// Pops the Epoch from the queue
    void PrePreparePopFront() override;

    /// Checks if the Epoch queue is empty
	///		@return true if empty false otherwise
    bool InternalQueueEmpty() override;

    /// Internal list (i.e. not in message handler) contains request with the hash
    /// @param request's hash
    /// @returns true if the request is in the list
    bool InternalContains(const BlockHash&) override;

	/// Gets secondary timeout value in seconds
	///     @return Seconds
	const Seconds & GetSecondaryTimeout() override;

    /// Create specialized instance of BackupDelegate
    ///     @param iochannel NetIOChannel pointer
    ///     @param ids Delegate's id
    ///     @return BackupDelegate
    std::shared_ptr<BackupDelegate<ConsensusType::Epoch>>
    MakeBackupDelegate(const DelegateIdentities& ids) override;

    /// Request's primary delegate, 0 (delegate with most voting power) for Micro/Epoch Block
    /// @param message request
    /// @returns designated delegate
    uint8_t DesignatedDelegate(std::shared_ptr<DelegateMessage> message) override;

	///  Check if backup already cleared primary's preprepare from main message handler
	///     @returns true if message is already cleared
	bool AlreadyPostCommitted() override;

private:

	MessageHandler<ConsensusType::Epoch> & GetHandler() override { return _handler; }
	EpochMessageHandler &  	 	 _handler;
    std::shared_ptr<PrePrepare>  _cur_epoch;          ///< Currently handled epoch
    std::recursive_mutex         _mutex;         	  ///< _cur_epoch mutex
	Seconds                      _secondary_timeout;  ///< Secondary list timeout value for this delegate
};
