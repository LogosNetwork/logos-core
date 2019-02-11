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
    ///     @param[in] validator validator/signer of consensus messages
    ///     @param[in] events_notifier epoch transition helper
    MicroBlockConsensusManager(Service & service,
                               Store & store,
                               const Config & config,
                               MessageValidator & validator,
                               ArchiverMicroBlockHandler & handler,
                               EpochEventsNotifier & events_notifier);

    ~MicroBlockConsensusManager() = default;

    /// Handles benchmark request.
    ///
    /// Special benchmark request.
    ///     @param[in]  block state block
    ///     @param[out] result result of the operation
    void OnBenchmarkSendRequest(
        std::shared_ptr<Request>,
        logos::process_return & ) override;

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
    ///     @param[in] block the micro block to commit to the database
    ///     @param[in] delegate_id delegate id
    void ApplyUpdates(
        const ApprovedMB &,
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
    void QueueRequestPrimary(std::shared_ptr<Request>) override;

    /// Gets next available MicroBlock.
    ///     @return reference to MicroBlock
    PrePrepare & PrePrepareGetNext() override;

    PrePrepare & PrePrepareGetCurr() override;

    ///< Pops the MicroBlock from the queue
    void PrePreparePopFront() override;

    ///< Checks if the MicroBlock queue is empty
	///		@return true if empty false otherwise
    bool PrePrepareQueueEmpty() override;

    /// Primary list contains request with the hash
    /// @param request's hash
    /// @returns true if the request is in the list
    bool PrimaryContains(const BlockHash&) override;

    /// Queue request in the secondary list
    /// @param request
    void QueueRequestSecondary(std::shared_ptr<Request>) override;

    /// Create specialized instance of BackupDelegate
    ///     @param iochannel NetIOChannel pointer
    ///     @param ids Delegate's id
    ///     @return BackupDelegate
    std::shared_ptr<BackupDelegate<ConsensusType::MicroBlock>> MakeBackupDelegate(
            std::shared_ptr<IOChannel> iochannel, const DelegateIdentities& ids) override;

private:

    std::shared_ptr<PrePrepare>  _cur_microblock;     ///< Currently handled microblock
    ArchiverMicroBlockHandler &  _microblock_handler; ///< Is used for validation and database commit
	int                          _enqueued;           ///< Request is enqueued
};
