#pragma once

#include <logos/consensus/persistence/persistence_manager_incl.hpp>
#include <logos/wallet_server/client/wallet_server_client.hpp>
#include <logos/consensus/batchblock/request_handler.hpp>
#include <logos/consensus/secondary_request_handler.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/p2p/consensus_p2p_bridge.hpp>
#include <logos/consensus/consensus_msg_producer.hpp>
#include <logos/consensus/backup_delegate.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/node/client_callback.hpp>

#include <boost/log/sources/record_ostream.hpp>

class EpochEventsNotifier;

class NetIOHandler
{

public:

    NetIOHandler() = default;

    virtual ~NetIOHandler() = default;

    virtual
    std::shared_ptr<ConsensusMsgSink>
    BindIOChannel(std::shared_ptr<IOChannel>,
                  const DelegateIdentities &) = 0;
    virtual void OnNetIOError(uint8_t delegate_id) = 0;
};

template<ConsensusType CT>
class RequestPromoter
{

    using Store      = logos::block_store;

protected:

    using Request    = RequestMessage<CT>;
    using PrePrepare = PrePrepareMessage<CT>;

public:

    virtual void OnRequestReady(std::shared_ptr<Request> block) = 0;
    virtual void OnPostCommit(const PrePrepare & block) = 0;
    virtual Store & GetStore() = 0;

    virtual void AcquirePrePrepare(const PrePrepare & message) {}

    virtual ~RequestPromoter() {}
};

template<ConsensusType CT>
class ConsensusManager : public PrimaryDelegate,
                         public NetIOHandler,
                         public RequestPromoter<CT>,
                         public ConsensusP2pBridge<CT>,
                         public ConsensusMsgProducer
{

protected:

    using Service     = boost::asio::io_service;
    using Config      = ConsensusManagerConfig;
    using Store       = logos::block_store;
    using Connection  = BackupDelegate<CT>;
    using Connections = std::vector<std::shared_ptr<Connection>>;
    using Manager     = ConsensusManager<CT>;
    using Request     = RequestMessage<CT>;
    using PrePrepare  = PrePrepareMessage<CT>;
    using PostPrepare = PostPrepareMessage<CT>;
    using PostCommit  = PostCommitMessage<CT>;
    using ReservationsPtr = std::shared_ptr<ReservationsProvider>;
    using ApprovedBlock   = PostCommittedBlock<CT>;
    using Responses   = std::vector<std::pair<logos::process_result, BlockHash>>;

public:

    ConsensusManager(Service & service,
                     Store & store,
                     const Config & config,
                     MessageValidator & validator,
                     EpochEventsNotifier & events_notifier,
                     p2p_interface & p2p);

    void HandleRequest(std::shared_ptr<Request> block,
                       BlockHash &hash,
                       logos::process_return & result);

    void OnSendRequest(std::shared_ptr<Request> block,
                       logos::process_return & result);

    Responses OnSendRequest(std::vector<std::shared_ptr<Request>>& blocks);

    void OnRequestQueued();

    virtual void OnBenchmarkSendRequest(std::shared_ptr<Request> block,
                                        logos::process_return & result) = 0;

    void Send(const void * data, size_t size) override;

    virtual ~ConsensusManager()
    {
        LOG_DEBUG(_log) << "~ConsensusManager<" << ConsensusToName(CT) << ">";
    }

    void OnRequestReady(std::shared_ptr<Request> block) override;

    void OnPostCommit(const PrePrepare & block) override;

    Store & GetStore() override;

    std::shared_ptr<ConsensusMsgSink>
    BindIOChannel(std::shared_ptr<IOChannel>,
                  const DelegateIdentities &) override;

    /// Update secondary request handler promoter
    void UpdateRequestPromoter();

    void OnNetIOError(uint8_t delegate_id) override;

protected:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;
    static constexpr uint8_t DELIGATE_ID_MASK    = 5;

    virtual void ApplyUpdates(const ApprovedBlock &, uint8_t delegate_id) = 0;

    virtual bool Validate(std::shared_ptr<Request> block,
                          logos::process_return & result) = 0;

    virtual uint64_t GetStoredCount() = 0;

    void OnConsensusReached() override;
    virtual void InitiateConsensus();

    virtual bool ReadyForConsensus();

    void QueueRequest(std::shared_ptr<Request>);

    virtual PrePrepare & PrePrepareGetNext() = 0;
    virtual PrePrepare & PrePrepareGetCurr() = 0;

    virtual void PrePreparePopFront() {};
    virtual void QueueRequestPrimary(std::shared_ptr<Request>) = 0;
    virtual void QueueRequestSecondary(std::shared_ptr<Request>);
    virtual bool PrePrepareQueueEmpty() = 0;
    virtual bool PrimaryContains(const BlockHash&) = 0;
    virtual bool SecondaryContains(const BlockHash&);

    bool IsPendingRequest(std::shared_ptr<Request>);

    bool IsPrePrepared(const BlockHash & hash);

    /// Request's primary delegate, 0 (delegate with most voting power) for Micro/Epoch Block
    /// @param request request
    /// @returns designated delegate
    virtual uint8_t DesignatedDelegate(std::shared_ptr<Request> request)
    {
        return 0;
    }

    virtual std::shared_ptr<BackupDelegate<CT>> MakeBackupDelegate(
            std::shared_ptr<IOChannel>, const DelegateIdentities&) = 0;

    /// singleton secondary handler
    static SecondaryRequestHandler<CT> & SecondaryRequestHandlerInstance(
        Service & service,
        RequestPromoter<CT>* promoter)
    {
        // Promoter is assigned once when the object is constructed
        // Promoter is updated during transition
        static SecondaryRequestHandler<CT> handler(service, promoter);
        return handler;
    }

    bool AddToConsensusQueue(const uint8_t * data,
                             uint8_t version,
                             MessageType message_type,
                             ConsensusType consensus_type,
                             uint32_t payload_size,
                             uint8_t delegate_id=0xff) override;

    void SendP2p(const uint8_t *data, uint32_t size, uint32_t epoch_number, uint8_t dest_delegate_id) override
    {
        ConsensusP2pBridge<CT>::SendP2p(data, size, epoch_number, dest_delegate_id);
    }

    Service &                       _service;
    Connections                     _connections;
    Store &                         _store;
    MessageValidator &              _validator;
    std::mutex                      _connection_mutex;
    Log                             _log;
    SecondaryRequestHandler<CT> &   _secondary_handler;    ///< Secondary queue of blocks.
    EpochEventsNotifier &           _events_notifier;      ///< Notifies epoch manager of transition related events
    ReservationsPtr                 _reservations;
    PersistenceManager<CT>          _persistence_manager;
};

