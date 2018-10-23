/// @file
/// This file contains declaration of the BatchBlockConsensusManager class, which
/// handles specifics of BatchBlock consensus.
#pragma once

#include <logos/consensus/batchblock/bb_consensus_connection.hpp>
#include <logos/consensus/batchblock/request_handler.hpp>
#include <logos/consensus/consensus_manager.hpp>

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
    ///     @param[in] log reference to boost asio log.
    ///     @param[in] config reference to ConsensusManagerConfig.
    ///     @param[in] key_store stores delegates' public keys.
    ///     @param[in] validator validator/signer of consensus messages.
    BatchBlockConsensusManager(Service & service, 
                               Store & store,
                               Log & log,
                               const Config & config,
                               DelegateKeyStore & key_store,
                               MessageValidator & validator,
                               const std::string & callback_address,
                               const uint16_t & callback_port,
                               const std::string & callback_target);

    virtual ~BatchBlockConsensusManager() {};

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

    std::shared_ptr<PrequelParser>
    BindIOChannel(std::shared_ptr<IOChannel>,
                  const DelegateIdentities &) override;

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
    bool ReadyForConsensus() override;

    /// Returns number of stored blocks.
    ///
    /// Benchmarking related.
    ///     @return number of stored blocks
    uint64_t GetStoredCount() override;

    /// Sends buffered blocks.
    ///
    /// Benchmark related.
    void OnConsensusReached() override;

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
    void QueueRequestPrimary(
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

    /// Primary list contains request with the hash
    /// @param request's hash
    /// @returns true if the request is in the list
    bool PrimaryContains(const logos::block_hash&) override;

    /// Create specialized instance of ConsensusConnection
    ///     @param iochannel NetIOChannel pointer
    ///     @param primary PrimaryDelegate pointer
    ///     @param key_store Delegates' public key store
    ///     @param validator Validator/Signer of consensus messages
    ///     @param ids Delegate's id
    ///     @return ConsensusConnection
    std::shared_ptr<ConsensusConnection<ConsensusType::BatchStateBlock>> MakeConsensusConnection(
            std::shared_ptr<IOChannel> iochannel, const DelegateIdentities& ids) override;

    uint8_t DesignatedDelegate(std::shared_ptr<Request> request) override;

private:

    bool                    _using_buffered_blocks = false; ///< Flag to indicate if buffering is enabled - benchmark related.
    BlockBuffer             _buffer;                        ///< Buffered state blocks.
    RequestHandler          _handler;                       ///< Primary queue of batch state blocks.
    PersistenceManager		_persistence_manager;			///< Database interface and request validation
};
