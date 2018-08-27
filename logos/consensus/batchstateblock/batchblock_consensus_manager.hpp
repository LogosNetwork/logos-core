/// @file
/// This file contains declaration of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus.
#pragma once

#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/request_handler.hpp>

/// ConsensusManager that handles BatchBlock consensus.
class BatchBlockConsensusManager: public ConsensusManager<ConsensusType::BatchStateBlock>
{

    using BlockBuffer = std::list<std::shared_ptr<Request>>;

public:

    /// Class constructor.
    ///
    /// This constructor is called by ConsensusContainer.
    ///     @param[in] service reference to boost asio service.
    ///     @param[in] store reference to blockstore.
    ///     @param[in] alarm reference to alarm.
    ///     @param[in] log reference to boost asio log.
    ///     @param[in] config reference to ConsensusManagerConfig.
    ///     @param[in] key_store stores delegates' public keys.
    ///     @param[in] validator validator/signer of consensus messages.
    BatchBlockConsensusManager(Service & service, 
                               Store & store,
                               logos::alarm & alarm,
                               Log & log,
                               const Config & config,
                               DelegateKeyStore & key_store,
                               MessageValidator & validator)
        : Manager(service, store, alarm, log,
                  config, key_store, validator)
    {}

    ~BatchBlockConsensusManager() = default;

    /// Handles benchmark requests.
    ///     @param[in]  block state block.
    ///     @param[out] result result of the operation.
    void OnBenchmarkSendRequest(std::shared_ptr<Request> block,
                                logos::process_return & result) override;

    /// Called to indicate that the buffering is complete.
    ///
    /// This method is only used during the benchmarking
    /// effort.
    ///     @param[out] result result of the operation
    void BufferComplete(logos::process_return & result);

protected:

    /// Commit the block to the store.
    ///
    /// Commits the block to the database.
    ///     @param block the batch block to commit to the database
    ///     @param delegate_id delegate id
    void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id) override;

    /// Checks if the system is ready to initiate consensus.
    ///
    ///  The extended override does additional processing if _using_buffered_blocks is true
    ///      @return true if ready false otherwise.
    bool ReadyForConsensusExt() override;

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

    /// Validates state block.
    ///     @param result of the validation
    ///     @return true if validated false otherwise
    bool Validate(
        std::shared_ptr<Request> block,
        logos::process_return & result) override;

    /// Sends buffered blocks.
    ///
    /// Benchmark related.
    void SendBufferedBlocks();

    /// Queues request message.
    ///
    /// Queues state block.
    void QueueRequest(
        std::shared_ptr<Request>) override;

    /// Gets next available BatchStateBlock.
    ///     @return reference to BatchStateBlock
    PrePrepare & PrePrepareGetNext() override;

    /// Pops the BatchStateBlock from the queue.
    void PrePreparePopFront() override;

    /// Checks if the BatchStateBlock queue is empty.
    ///
    ///     @return true if empty false otherwise
    bool PrePrepareQueueEmpty() override;

    /// Checks if the BatchStateBlock queue is full.
    ///
    ///     @return true if full false otherwise
    bool PrePrepareQueueFull() override;

private:

    bool           _using_buffered_blocks = false; ///< Flag to indicate if buffering is enabled - benchmark related.
    BlockBuffer    _buffer;                        ///< Buffered state blocks.
    RequestHandler _handler;                       ///< Queue of batch state blocks.
};
