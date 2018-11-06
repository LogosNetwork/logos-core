/// @file
/// This file contains declaration of the MicroBlockConsensusManager class, which
/// handles specifics of MicroBlock consensus.
#pragma once

#include <logos/consensus/consensus_manager.hpp>

class ArchiverMicroBlockHandler;

/// MicroBlockConsensusManager handles the specifics of MicroBlock consensus.
class MicroBlockConsensusManager: public ConsensusManager<ConsensusType::MicroBlock>
{

public:

    /// Class constructor
    ///
    /// Called by ConsensusContainer.
    ///     @param[in] service reference to boost asio service
    ///     @param[in] store reference to blockstore
    ///     @param[in] log reference to boost asio log
    ///     @param[in] config reference to ConsensusManagerConfig configuration
    ///     @param[in] key_store delegates public key store
    ///     @param[in] validator validator/signer of consensus messages
    MicroBlockConsensusManager(Service & service,
                               Store & store,
                               Log & log,
                               const Config & config,
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
			       ArchiverMicroBlockHandler & handler,
			       p2p_interface & p2p)
       : Manager(service, store, log,
		  config, key_store, validator, p2p)
        , _microblock_handler(handler)
        , _enqueued(false)
    {
	}

    ~MicroBlockConsensusManager() = default;

    /// Handles benchmark request.
    ///
    /// Special benchmark request.
    ///     @param[in]  block state block
    ///     @param[out] result result of the operation
    void OnBenchmarkSendRequest(
        std::shared_ptr<Request>,
        logos::process_return & ) override;

protected:

    /// Commit to the store.
    ///
    /// Commits the block to the database.
    ///     @param[in] block the micro block to commit to the database
    ///     @param[in] delegate_id delegate id
    void ApplyUpdates(
        const PrePrepare &,
        uint8_t delegate_id) override;

    /// Returns number of stored blocks.
    ///
    /// Benchmarking related.
    ///     @return number of stored blocks
    uint64_t GetStoredCount() override;

    /// Validates state blocks.
    ///     @param[in]  block the block to be validated
    ///     @param[out] result of the validation
    ///     @return true if validated false otherwise
    bool Validate(
        std::shared_ptr<Request> block,
        logos::process_return & result) override;

    /// Validates PrePrepare message.
    ///     @return true if validated false otherwise
    bool Validate(const PrePrepare & message,
	uint8_t delegate_id) override;

    /// Queues micro block.
    void QueueRequestPrimary(std::shared_ptr<Request>) override;

    /// Gets next available MicroBlock.
    ///     @return reference to MicroBlock
    PrePrepare & PrePrepareGetNext() override;

    ///< Pops the MicroBlock from the queue
    void PrePreparePopFront() override;

    ///< Checks if the MicroBlock queue is empty
	///		@return true if empty false otherwise
    bool PrePrepareQueueEmpty() override;

    /// Checks if the MicroBlock queue is full.
    ///     @return true if full false otherwise
    bool PrePrepareQueueFull() override;

    /// Primary list contains request with the hash
    /// @param request's hash
    /// @returns true if the request is in the list
    bool PrimaryContains(const logos::block_hash&) override;

    /// Queue request in the secondary list
    /// @param request
    void QueueRequestSecondary(std::shared_ptr<Request>) override;

    /// Create specialized instance of ConsensusConnection
    ///     @param iochannel NetIOChannel pointer
    ///     @param primary PrimaryDelegate pointer
    ///     @param key_store Delegates' public key store
    ///     @param validator Validator/Signer of consensus messages
    ///     @param ids Delegate's id
    ///     @return ConsensusConnection
    std::shared_ptr<ConsensusConnection<ConsensusType::MicroBlock>> MakeConsensusConnection(
            std::shared_ptr<IOChannel> iochannel, const DelegateIdentities& ids) override;
private:

    std::shared_ptr<PrePrepare>  _cur_microblock;     ///< Currently handled microblock
    ArchiverMicroBlockHandler &  _microblock_handler; ///< Is used for validation and database commit
	int                          _enqueued;           ///< Request is enqueued
};
