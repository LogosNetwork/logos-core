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
					      const Config & config,
                          MessageValidator & validator,
                          EpochEventsNotifier & events_notifier);

    ~EpochConsensusManager() = default;

	/// Handles benchmark request.
	///
	/// Special benchmark request.
	///     @param[in]  epoch block
	///     @param[out] result result of the operation
    void OnBenchmarkSendRequest(
		std::shared_ptr<Request>,
		logos::process_return & ) override;

protected:
	/// Commit to the store.
	///
	/// Commits the block to the database.
	///     @param[in] block the epoch block to commit to the database
	///     @param[in] delegate_id delegate id
	void ApplyUpdates(
		const ApprovedEB &,
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
		std::shared_ptr<Request> block,
		logos::process_return & result) override;

    /// Queues epoch block
	void QueueRequestPrimary(std::shared_ptr<Request>) override;

    /// Gets next available epoch block
	///		@return reference to epoch block
    PrePrepare & PrePrepareGetNext() override;

    PrePrepare & PrePrepareGetCurr() override;

    /// Pops the Epoch from the queue
    void PrePreparePopFront() override;

    /// Checks if the Epoch queue is empty
	///		@return true if empty false otherwise
    bool PrePrepareQueueEmpty() override;

    /// Primary list contains request with the hash
    /// @param request's hash
    /// @returns true if the request is in the list
    bool PrimaryContains(const BlockHash&) override;

	void QueueRequestSecondary(std::shared_ptr<Request> request) override;

	/// Create specialized instance of ConsensusConnection
	///     @param iochannel NetIOChannel pointer
	///     @param ids Delegate's id
	///     @return ConsensusConnection
	std::shared_ptr<ConsensusConnection<ConsensusType::Epoch>> MakeConsensusConnection(
			std::shared_ptr<IOChannel> iochannel, const DelegateIdentities& ids) override;

	/// Request's primary delegate, 0 (delegate with most voting power) for Micro/Epoch Block
    /// @param request request
    /// @returns designated delegate
    uint8_t DesignatedDelegate(std::shared_ptr<Request> request) override;

private:
    std::shared_ptr<PrePrepare>  _cur_epoch; 	///< Currently handled epoch
	bool 						 _enqueued;   	///< Request is enqueued
};
