/// @file
/// This file contains declaration of the RequestConsensusManager class, which
/// handles specifics of Request consensus.
#pragma once

#include <logos/consensus/request/request_backup_delegate.hpp>
#include <logos/consensus/request/request_internal_queue.hpp>
#include <logos/consensus/consensus_manager.hpp>

/// ConsensusManager that handles Request consensus.
class RequestConsensusManager: public ConsensusManager<ConsensusType::Request>
{

    using BlockBuffer = std::list<std::shared_ptr<DelegateMessage>>;
    using Rejection   = RejectionMessage<ConsensusType::Request>;
    using Prepare     = PrepareMessage<ConsensusType::Request>;
    using Seconds     = boost::posix_time::seconds;
    using Timer       = boost::asio::deadline_timer;
    using Error       = boost::system::error_code;
    using Hashes      = std::unordered_set<BlockHash>;
    using uint128_t   = logos::uint128_t;

    // Pairs a set of Delegate ID's with indexes,
    // where the indexes represent the requests
    // supported by those delegates.
    using SupportMap  = std::pair<std::unordered_set<uint8_t>,
                                  std::unordered_set<uint64_t>>;

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
                            Cache & block_cache,
                            const Config & config,
                            ConsensusScheduler & scheduler,
                            MessageValidator & validator,
                            p2p_interface & p2p,
                            uint32_t epoch_number,
                            EpochHandler & epoch_handler);

    ~RequestConsensusManager() = default;

    /// Handles benchmark requests.
    ///     @param[in]  message message.
    ///     @param[out] result result of the operation.
    void OnBenchmarkDelegateMessage(std::shared_ptr<DelegateMessage> message,
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
    std::shared_ptr<MessageParser>
    BindIOChannel(std::shared_ptr<IOChannel> iochannel,
                  const DelegateIdentities & ids) override;


    void StartConsensusWithP2p();

    bool DelegatesConnected()
    {
        return _delegates_connected;
    }

    template<typename F>
    static std::list<SupportMap> GenerateSubsets(uint128_t vote,
                                                 uint128_t stake,
                                                 uint64_t request_count,
                                                 const WeightList & weights,
                                                 const F & reached_quorum);

protected:

    /// Commit the block to the store.
    ///
    /// Commits the block to the database.
    ///     @param block the batch block to commit to the database
    ///     @param delegate_id delegate id
    void ApplyUpdates(const ApprovedRB & block, uint8_t delegate_id) override;

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
        std::shared_ptr<DelegateMessage> message,
        logos::process_return & result) override;

    /// Sends buffered blocks.
    ///
    /// Benchmark related.
    void SendBufferedBlocks();

    /// Gets next available BatchStateBlock.
    ///     @return reference to BatchStateBlock
    PrePrepare & PrePrepareGetNext(bool) override;

    PrePrepare & PrePrepareGetCurr() override;

    void ConstructBatch(bool);

    /// Pops the BatchStateBlock from the queue.
    void PrePreparePopFront() override;

    /// Gets secondary timeout value in seconds
    ///     @return Seconds
    const Seconds & GetSecondaryTimeout() override;

    /// Checks if the Request queue is empty.
    ///
    /// This performs additional processing if
    /// _using_buffered_blocks is true.
    ///     @return true if empty false otherwise
    bool InternalQueueEmpty() override;

    /// Internal queue (i.e. not in message handler) contains request with the hash
    /// @param request's hash
    /// @returns true if the request is in the queue
    bool InternalContains(const BlockHash &) override;

    /// Create specialized instance of BackupDelegate
    ///     @param iochannel NetIOChannel pointer
    ///     @param ids Delegate's id
    ///     @return BackupDelegate
    std::shared_ptr<BackupDelegate<ConsensusType::Request>>
    MakeBackupDelegate(const DelegateIdentities& ids) override;

    /// Find Primary delegate index for this request
    /// @param message message
    /// @returns delegate's index
    uint8_t DesignatedDelegate(std::shared_ptr<DelegateMessage> message) override;

private:

    static const Seconds ON_CONNECTED_TIMEOUT;
    static const Seconds REQUEST_TIMEOUT;

    MessageHandler<R> & GetHandler() override { return _handler; }
    void TallyPrepareMessage(const Prepare & message, uint8_t remote_delegate_id) override;
    void OnRejection(const Rejection & message, uint8_t remote_delegate_id) override;
    void OnStateAdvanced() override;
    bool IsPrePrepareRejected() override;
    void OnPrePrepareRejected() override;

    void OnDelegatesConnected();
    void StartConsensus(bool enable_p2p);

    bool Rejected(uint128_t reject_vote, uint128_t reject_stake);

    RequestMessageHandler &  _handler;         ///< Queue of requests/proposals.
    WeightList            _response_weights;
    Hashes                _hashes;                        ///< keeps track of which request in cur batch hasn't been explictly accepted or rejected
    bool                  _repropose_subset      = false; ///< indicator of whether a Contains_Invalid_Request Rejection has been received
    BlockBuffer           _buffer;                        ///< Buffered state blocks.
    std::mutex            _buffer_mutex;                  ///< SYL Integration fix: separate lock for benchmarking buffer
    Timer                 _init_timer;
    uint64_t              _sequence              = 0;
    uint128_t             _connected_vote        = 0;
    uint128_t             _connected_stake       = 0;
    uint128_t             _ne_reject_vote        = 0;     ///< New Epoch rejection vote weight.
    uint128_t             _ne_reject_stake       = 0;     ///< New Epoch rejection stake weight.
    bool                  _using_buffered_blocks = false; ///< Flag to indicate if buffering is enabled - benchmark related.
    //The below bool members should be accessed under a mutex
    bool                  _delegates_connected   = false;
    bool                  _started_consensus     = false;
    // No need for mutex protecting request queue or curr batch as all accesses are serialized (only accessible when _ongoing)
    PrePrepare            _current_batch;
    RequestInternalQueue  _request_queue;
    Seconds               _secondary_timeout;             ///< Secondary list timeout value for this delegate
};
