#pragma once

#include <logos/consensus/persistence/persistence_manager_incl.hpp>
#include <logos/wallet_server/client/wallet_server_client.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/p2p/consensus_p2p_bridge.hpp>
#include <logos/consensus/message_handler.hpp>
#include <logos/consensus/consensus_msg_producer.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/backup_delegate.hpp>
#include <logos/consensus/p2p/consensus_p2p.hpp>
#include <logos/node/client_callback.hpp>

#include <boost/log/sources/record_ostream.hpp>

class EpochEventsNotifier;
class ConsensusScheduler;

class NetIOHandler
{

public:

    NetIOHandler() = default;

    virtual ~NetIOHandler() = default;

    virtual
    std::shared_ptr<MessageParser>
    BindIOChannel(std::shared_ptr<IOChannel>,
                  const DelegateIdentities &) = 0;
    virtual void OnNetIOError(uint8_t delegate_id) = 0;

    virtual std::shared_ptr<MessageParser>
        AddBackupDelegate(const DelegateIdentities&) = 0;
    
    virtual void DestroyAllBackups() = 0;

};

template<ConsensusType CT>
class ConsensusManager : public PrimaryDelegate,
                         public NetIOHandler,
                         public ConsensusP2pBridge
{

protected:

    using Service         = boost::asio::io_service;
    using Config          = ConsensusManagerConfig;
    using Store           = logos::block_store;
    using Cache           = logos::IBlockCache;
    using Connection      = BackupDelegate<CT>;
    using Connections     = std::vector<std::shared_ptr<Connection>>;
    using Manager         = ConsensusManager<CT>;
    using DelegateMessage = ::DelegateMessage<CT>;
    using PrePrepare      = PrePrepareMessage<CT>;
    using PostPrepare     = PostPrepareMessage<CT>;
    using PostCommit      = PostCommitMessage<CT>;
    using ReservationsPtr = std::shared_ptr<Reservations>;
    using ApprovedBlock   = PostCommittedBlock<CT>;
    using Responses       = std::vector<std::pair<logos::process_result, BlockHash>>;
    using ErrorCode       = boost::system::error_code;
    template <typename T>
    using WPTR            = std::weak_ptr<T>;

    using Seconds         = boost::posix_time::seconds;

public:

    ConsensusManager(Service & service,
                     Store & store,
                     Cache & block_cache,
                     const Config & config,
                     ConsensusScheduler & scheduler,
                     MessageValidator & validator,
                     p2p_interface & p2p,
                     uint32_t epoch_number);

    void HandleRequest(std::shared_ptr<DelegateMessage> message,
                       BlockHash &hash,
                       logos::process_return & result);

    void OnDelegateMessage(std::shared_ptr<DelegateMessage> message,
                           logos::process_return & result);

    Responses OnSendRequest(std::vector<std::shared_ptr<DelegateMessage>>& blocks);

    void OnMessageQueued();

    virtual void OnBenchmarkDelegateMessage(std::shared_ptr<DelegateMessage> block,
                                            logos::process_return & result) = 0;

    void Send(const void * data, size_t size) override;

    virtual ~ConsensusManager()
    {
        LOG_DEBUG(_log) << "~ConsensusManager<" << ConsensusToName(CT) << ">";
    }

    std::shared_ptr<MessageParser>
    BindIOChannel(std::shared_ptr<IOChannel>,
                  const DelegateIdentities &) override;

    std::shared_ptr<MessageParser> AddBackupDelegate(const DelegateIdentities & ids) override;

    void OnNetIOError(uint8_t delegate_id) override;

    void ClearMessageList()
    {
        GetHandler().Clear();
    }

    void Init(std::shared_ptr<EpochEventsNotifier> notifier)
    {
        _events_notifier = notifier;
    }


    void EnableP2p(bool enable) override;

    void DestroyAllBackups();


protected:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;
    static constexpr uint8_t DELIGATE_ID_MASK    = 5;

    virtual MessageHandler<CT> & GetHandler() = 0;
    virtual void ApplyUpdates(const ApprovedBlock &, uint8_t delegate_id) = 0;

    virtual bool Validate(std::shared_ptr<DelegateMessage> block,
                          logos::process_return & result) = 0;

    virtual uint64_t GetStoredCount() = 0;

    void OnConsensusReached() override;
    void BeginNextRound();
    void InitiateConsensus(bool reproposing = false);

    void QueueMessage(std::shared_ptr<DelegateMessage> message);

    virtual PrePrepare & PrePrepareGetNext(bool) = 0;
    virtual PrePrepare & PrePrepareGetCurr() = 0;

    virtual void PrePreparePopFront() {};
    void QueueMessagePrimary(std::shared_ptr<DelegateMessage> message);
    void QueueMessageSecondary(std::shared_ptr<DelegateMessage> message);
    virtual const Seconds & GetSecondaryTimeout() = 0;
    bool PrePrepareQueueEmpty();
    virtual bool InternalQueueEmpty() = 0;
    virtual bool InternalContains(const BlockHash&) = 0;
    bool Contains(const BlockHash&);
    virtual bool AlreadyPostCommitted() {return false;}

    bool IsPendingMessage(std::shared_ptr<DelegateMessage> message);

    /// Message's primary delegate, 0 (delegate with most voting power) for Micro/Epoch Block
    /// @param message message
    /// @returns designated delegate
    virtual uint8_t DesignatedDelegate(std::shared_ptr<DelegateMessage> message)
    {
        return 0;
    }

    virtual std::shared_ptr<BackupDelegate<CT>> MakeBackupDelegate(
            const DelegateIdentities&) = 0;

    bool SendP2p(const uint8_t *data, uint32_t size, MessageType message_type,
                 uint32_t epoch_number, uint8_t dest_delegate_id) override
    {
        return ConsensusP2pBridge::SendP2p(data, size, message_type, epoch_number, dest_delegate_id);
    }


    void OnP2pTimeout(const ErrorCode &);

    void OnQuorumFailed() override;

    bool ProceedWithRePropose();

    Service &                 _service;
    Connections               _connections;
    Store &                   _store;
    Cache &                   _block_cache;
    MessageValidator &        _validator;
    ConsensusScheduler &      _scheduler;
    std::mutex                _connection_mutex;
    Log                       _log;
    WPTR<EpochEventsNotifier> _events_notifier; ///< Notifies epoch manager of transition related events
    ReservationsPtr           _reservations;
    PersistenceManager<CT>    _persistence_manager;
};

