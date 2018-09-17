/// @file
/// This file contains declaration of the MicroBlockConsensusManager class, which
/// handles specifics of MicroBlock consensus.
#pragma once

#include <logos/consensus/consensus_manager.hpp>

class IArchiverMicroBlockHandler;

/// MicroBlockConsensusManager handles the specifics of MicroBlock consensus.
class MicroBlockConsensusManager: public ConsensusManager<ConsensusType::MicroBlock>
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
    MicroBlockConsensusManager(Service & service,
                               Store & store,
                               logos::alarm & alarm,
                               Log & log,
                               const Config & config,
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               IArchiverMicroBlockHandler & handler)
        : Manager(service, store, alarm, log,
                  config, key_store, validator)
        , _microblock_handler(handler)
    {
		queue = 1;
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

    /// Queues micro block.
    void QueueRequest(std::shared_ptr<Request>) override;

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

    /// Create specialized instance of ConsensusConnection
    ///     @param iochannel NetIOChannel pointer
    ///     @param primary PrimaryDelegate pointer
    ///     @param key_store Delegates' public key store
    ///     @param validator Validator/Signer of consensus messages
    ///     @param ids Delegate's id
    ///     @return ConsensusConnection
    std::shared_ptr<ConsensusConnection<ConsensusType::MicroBlock>> MakeConsensusConnection(
            std::shared_ptr<IOChannel> iochannel, PrimaryDelegate* primary,
            MessageValidator& validator, const DelegateIdentities& ids) override;
private:

    std::shared_ptr<PrePrepare>  _cur_microblock;     ///< Currently handled microblock
    IArchiverMicroBlockHandler & _microblock_handler; ///< Is used for validation and database commit
	int queue;
};