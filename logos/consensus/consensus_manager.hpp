#pragma once

#include <logos/consensus/persistence/persistence_manager_incl.hpp>
#include <logos/wallet_server/client/wallet_server_client.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/request/request_handler.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/backup_delegate.hpp>
#include <logos/consensus/waiting_list.hpp>
#include <logos/node/client_callback.hpp>

#include <boost/log/sources/record_ostream.hpp>

class EpochEventsNotifier;

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
};

template<ConsensusType CT>
class MessagePromoter
{

    using Store      = logos::block_store;

protected:

    using Message    = RequestMessage<CT>;
    using PrePrepare = PrePrepareMessage<CT>;

public:

    virtual void OnMessageReady(std::shared_ptr<Message> message) = 0;
    virtual void OnPostCommit(const PrePrepare & pre_prepare) = 0;
    virtual Store & GetStore() = 0;

    virtual void AcquirePrePrepare(const PrePrepare & message) {}

    virtual ~MessagePromoter() {}
};

template<ConsensusType CT>
class ConsensusManager : public PrimaryDelegate,
                         public NetIOHandler,
                         public MessagePromoter<CT>
{

protected:

    using Service         = boost::asio::io_service;
    using Config          = ConsensusManagerConfig;
    using Store           = logos::block_store;
    using Connection      = BackupDelegate<CT>;
    using Connections     = std::vector<std::shared_ptr<Connection>>;
    using Manager         = ConsensusManager<CT>;
    using DelegateMessage = RequestMessage<CT>;
    using PrePrepare      = PrePrepareMessage<CT>;
    using PostPrepare     = PostPrepareMessage<CT>;
    using PostCommit      = PostCommitMessage<CT>;
    using ReservationsPtr = std::shared_ptr<ReservationsProvider>;
    using ApprovedBlock   = PostCommittedBlock<CT>;

public:

    ConsensusManager(Service & service,
                     Store & store,
                     const Config & config,
                     MessageValidator & validator,
                     EpochEventsNotifier & events_notifier);

    void OnDelegateMessage(std::shared_ptr<DelegateMessage> message,
                           logos::process_return &result);

    void OnMessageQueued();

    virtual void OnBenchmarkDelegateMessage(std::shared_ptr<DelegateMessage> block,
                                            logos::process_return &result) = 0;

    void Send(const void * data, size_t size) override;

    virtual ~ConsensusManager()
    {
        LOG_DEBUG(_log) << "~ConsensusManager<" << ConsensusToName(CT) << ">";
    }

    void OnMessageReady(std::shared_ptr<DelegateMessage> block) override;

    void OnPostCommit(const PrePrepare & block) override;

    Store & GetStore() override;

    std::shared_ptr<MessageParser>
    BindIOChannel(std::shared_ptr<IOChannel>,
                  const DelegateIdentities &) override;

    /// Update message promoter
    void UpdateMessagePromoter();

    void OnNetIOError(uint8_t delegate_id) override;

protected:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;
    static constexpr uint8_t DELIGATE_ID_MASK    = 5;

    virtual void ApplyUpdates(const ApprovedBlock &, uint8_t delegate_id) = 0;

    virtual bool Validate(std::shared_ptr<DelegateMessage> block,
                          logos::process_return & result) = 0;

    virtual uint64_t GetStoredCount() = 0;

    void OnConsensusReached() override;
    virtual void InitiateConsensus();

    virtual bool ReadyForConsensus();

    void QueueMessage(std::shared_ptr<DelegateMessage> message);

    virtual PrePrepare & PrePrepareGetNext() = 0;
    virtual PrePrepare & PrePrepareGetCurr() = 0;

    virtual void PrePreparePopFront() {};
    virtual void QueueMessagePrimary(std::shared_ptr<DelegateMessage> message) = 0;
    virtual void QueueMessageSecondary(std::shared_ptr<DelegateMessage> message);
    virtual bool PrePrepareQueueEmpty() = 0;
    virtual bool PrimaryContains(const BlockHash&) = 0;
    virtual bool SecondaryContains(const BlockHash&);

    bool IsPendingMessage(std::shared_ptr<DelegateMessage> message);

    bool IsPrePrepared(const BlockHash & hash);

    /// Message's primary delegate, 0 (delegate with most voting power) for Micro/Epoch Block
    /// @param message message
    /// @returns designated delegate
    virtual uint8_t DesignatedDelegate(std::shared_ptr<DelegateMessage> message)
    {
        return 0;
    }

    virtual std::shared_ptr<BackupDelegate<CT>> MakeBackupDelegate(
            std::shared_ptr<IOChannel>, const DelegateIdentities&) = 0;

    /// singleton secondary handler
    static WaitingList<CT> & GetWaitingList(
        Service & service,
        MessagePromoter<CT>* promoter)
    {
        // Promoter is assigned once when the object is constructed
        // Promoter is updated during transition
        static WaitingList<CT> wl(service, promoter);
        return wl;
    }

    Connections            _connections;
    Store &                _store;
    MessageValidator &     _validator;
    std::mutex             _connection_mutex;
    Log                    _log;
    WaitingList<CT> &      _waiting_list;    ///< Secondary queue of requests/proposals.
    EpochEventsNotifier &  _events_notifier; ///< Notifies epoch manager of transition related events
    ReservationsPtr        _reservations;
    PersistenceManager<CT> _persistence_manager;
};

