/// @file
/// This file contains declaration of the RequestConsensusManager class, which
/// handles specifics of Request consensus.
#pragma once

#include <logos/consensus/request/request_backup_delegate.hpp>
#include <logos/consensus/request/request_handler.hpp>
#include <logos/consensus/consensus_manager.hpp>

/// ConsensusManager that handles Request consensus.
class RequestConsensusManager: public ConsensusManager<ConsensusType::Request>
{

    using BlockBuffer = std::list<std::shared_ptr<Request>>;
    using Rejection   = RejectionMessage<ConsensusType::Request>;
    using Prepare     = PrepareMessage<ConsensusType::Request>;
    using Seconds     = boost::posix_time::seconds;
    using Timer       = boost::asio::deadline_timer;
    using Error       = boost::system::error_code;
    using Hashes      = std::unordered_set<BlockHash>;
    using uint128_t   = logos::uint128_t;

    struct Weights
    {
        using Delegates = std::unordered_set<uint8_t>;

        uint128_t reject_vote            = 0;
        uint128_t reject_stake           = 0;
        uint128_t indirect_vote_support  = 0;
        uint128_t indirect_stake_support = 0;
        Delegates supporting_delegates;
    };

    using WeightList = std::array<Weights, CONSENSUS_BATCH_SIZE>;

public:

    /// Class constructor.
    ///
    /// This constructor is called by ConsensusContainer.
    ///     @param[in] service reference to boost asio service.
    ///     @param[in] store reference to blockstore.
    ///     @param[in] log reference to boost asio log.
    ///     @param[in] config reference to ConsensusManagerConfig.
    ///     @param[in] validator validator/signer of consensus messages.
    ///     @param[in] events_notifier transition helper
    RequestConsensusManager(Service & service,
                            Store & store,
                            const Config & config,
                            MessageValidator & validator,
                            EpochEventsNotifier & events_notifier,
                            p2p_interface & p2p);

    virtual ~RequestConsensusManager() {};

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

    /// Called to bind a BackupDelegate to a
    /// ConsensusNetIO.
    ///
    /// This is an overridden method that is specialized
    /// for BatchBlock Consensus.
    ///     @param[in] iochannel pointer to ConsensusNetIO
    ///                interface.
    ///     @param[in] ids Delegate IDs for the local and
    ///                remote delegates.
    std::shared_ptr<ConsensusMsgSink>
    BindIOChannel(std::shared_ptr<IOChannel> iochannel,
                  const DelegateIdentities & ids) override;

protected:

    /// Commit the block to the store.
    ///
    /// Commits the block to the database.
    ///     @param block the batch block to commit to the database
    ///     @param delegate_id delegate id
    void ApplyUpdates(const ApprovedBSB & block, uint8_t delegate_id) override;

    /// Checks if the system is ready to initiate consensus.
    ///
    ///  The extended override does additional processing if
    ///  _using_buffered_blocks is true.
    ///      @return true if ready false otherwise.
    bool ReadyForConsensus() override;

    /// Returns number of stored blocks.
    ///
    /// Benchmarking related.
    ///     @return number of stored blocks
    uint64_t GetStoredCount() override;

    void InitiateConsensus(bool reproposing = false) override;

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

    PrePrepare & PrePrepareGetCurr() override;

    /// Pops the BatchStateBlock from the queue.
    void PrePreparePopFront() override;

    /// Checks if the BatchStateBlock queue is empty.
    ///
    ///     @return true if empty false otherwise
    bool PrePrepareQueueEmpty() override;

    /// Primary list contains request with the hash
    /// @param request's hash
    /// @returns true if the request is in the list
    bool PrimaryContains(const BlockHash &) override;

    void OnPostCommit(const PrePrepare & block) override;

    /// Create specialized instance of BackupDelegate
    ///     @param iochannel NetIOChannel pointer
    ///     @param ids Delegate's id
    ///     @return BackupDelegate
    std::shared_ptr<BackupDelegate<ConsensusType::Request>> MakeBackupDelegate(
            std::shared_ptr<IOChannel> iochannel, const DelegateIdentities& ids) override;

    /// Find Primary delegate index for this request
    /// @param request request
    /// @returns delegate's index
    uint8_t DesignatedDelegate(std::shared_ptr<Request> request) override;

private:

    static const Seconds ON_CONNECTED_TIMEOUT;

    void AcquirePrePrepare(const PrePrepare & message) override;

    void TallyPrepareMessage(const Prepare & message, uint8_t remote_delegate_id) override;
    void OnRejection(const Rejection & message, uint8_t remote_delegate_id) override;
    void OnStateAdvanced() override;
    bool IsPrePrepareRejected() override;
    void OnPrePrepareRejected() override;

    void OnDelegatesConnected();

    bool Rejected(uint128_t reject_vote, uint128_t reject_stake);

    WeightList            _response_weights;
    Hashes                _hashes;
    bool                  _should_repropose      = false; ///< indicator of whether a Contains_Invalid_Request Rejection has been received
    BlockBuffer           _buffer;                        ///< Buffered state blocks.
    std::mutex            _buffer_mutex;                  ///< SYL Integration fix: separate lock for benchmarking buffer
    static RequestHandler _handler;                       ///< Primary queue of batch state blocks.
    Timer                 _init_timer;
    uint64_t              _sequence              = 0;
    uint128_t             _connected_vote        = 0;
    uint128_t             _connected_stake       = 0;
    uint128_t             _ne_reject_vote        = 0;     ///< New Epoch rejection vote weight.
    uint128_t             _ne_reject_stake       = 0;     ///< New Epoch rejection stake weight.
    bool                  _using_buffered_blocks = false; ///< Flag to indicate if buffering is enabled - benchmark related.
    bool                  _delegates_connected   = false;
};
