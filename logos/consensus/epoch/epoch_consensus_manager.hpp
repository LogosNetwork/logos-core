/// @file
/// This file contains declaration of the EpochConsensusManager class, which
/// handles specifics of Epoch consensus
#pragma once

#include <logos/consensus/consensus_manager.hpp>

class IArchiverEpochHandler;

///< EpochConsensusManager handles specifics of Epoch consensus
class EpochConsensusManager: public ConsensusManager<ConsensusType::Epoch>
{
public:

	/// Class constructor
	///
	/// Called by ConsensusContainer.
	///     @param[in] service reference to boost asio service
	///     @param[in] store reference to blockstore
	///     @param[in] alarm reference to alarm
	///     @param[in] log reference to boost asio log
	///     @param[in] config reference to ConsensusManagerConfig configuration
	///     @param[in] key_store delegates public key store
	///     @param[in] validator validator/signer of consensus messages
	EpochConsensusManager(Service & service,
	                      Store & store,
	                      logos::alarm & alarm,
	                      Log & log,
					      const Config & config,
                          DelegateKeyStore & key_store,
                          MessageValidator & validator,
                          IArchiverEpochHandler & handler)
		: Manager(service, store, alarm, log,
				  config, key_store, validator)
		, _epoch_handler(handler)
	{
		queue = 1;
	}

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
		const PrePrepare &,
		uint8_t delegate_id) override;

	/// Returns number of stored blocks.
	///
	/// Benchmarking related.
	///     @return number of stored blocks
    uint64_t OnConsensusReachedStoredCount() override;

	/// Sends buffered blocks.
	///
	/// Benchmark related.
	///     @return true if using buffered blocks
    bool OnConsensusReachedExt() override;

	/// Validates epoch block.
	///     @param[in]  block the block to be validated
	///     @param[out] result of the validation
	///     @return true if validated false otherwise
	bool Validate(
		std::shared_ptr<Request> block,
		logos::process_return & result) override;

    /// Queues epoch block
	void QueueRequest(std::shared_ptr<Request>) override;

    /// Gets next available epoch block
	///		@return reference to epoch block
    PrePrepare & PrePrepareGetNext() override;

    /// Pops the Epoch from the queue
    void PrePreparePopFront() override;

    /// Checks if the Epoch queue is empty
	///		@return true if empty false otherwise
    bool PrePrepareQueueEmpty() override;

    ///< Checks if the Epoch queue is full
	///		@return true if full false otherwise
    bool PrePrepareQueueFull() override;

	/// Create specialized instance of ConsensusConnection
	///     @param iochannel NetIOChannel pointer
	///     @param primary PrimaryDelegate pointer
	///     @param key_store Delegates' public key store
	///     @param validator Validator/Signer of consensus messages
	///     @param ids Delegate's id
	///     @return ConsensusConnection
	std::shared_ptr<ConsensusConnection<ConsensusType::Epoch>> MakeConsensusConnection(
			std::shared_ptr<IIOChannel> iochannel, PrimaryDelegate* primary, DelegateKeyStore& key_store,
			MessageValidator& validator, const DelegateIdentities& ids) override;

private:
    std::shared_ptr<PrePrepare>  _cur_epoch; 	///< Currently handled epoch
    IArchiverEpochHandler &      _epoch_handler;///< Epoch handler
	int queue;
};