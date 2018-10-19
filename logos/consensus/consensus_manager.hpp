#pragma once

#include <logos/wallet_server/client/wallet_server_client.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/batchblock/request_handler.hpp>
#include <logos/consensus/secondary_request_handler.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/consensus_connection.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/primary_delegate.hpp>

#include <boost/log/sources/record_ostream.hpp>

class ChannelBinder
{

public:

  ChannelBinder() {}

  virtual ~ChannelBinder() {}

  virtual
  std::shared_ptr<PrequelParser>
  BindIOChannel(std::shared_ptr<IOChannel>,
                const DelegateIdentities &) = 0;
};

template<ConsensusType CT>
class RequestPromoter
{
protected:
    using Request     = RequestMessage<CT>;
    using PrePrepare  = PrePrepareMessage<CT>;
public:

    virtual void OnRequestReady(std::shared_ptr<Request> block) = 0;

    virtual void OnPrePrepare(const PrePrepare & block) = 0;

    virtual ~RequestPromoter() {}
};

template<ConsensusType CT>
class ConsensusManager : public PrimaryDelegate,
                         public ChannelBinder,
                         public RequestPromoter<CT>
{

protected:

    using Service     = boost::asio::io_service;
    using Config      = ConsensusManagerConfig;
    using Log         = boost::log::sources::logger_mt;
    using Connections = std::vector<std::shared_ptr<ConsensusConnection<CT>>>;
    using Store       = logos::block_store;
    using Manager     = ConsensusManager<CT>;
    using Request     = RequestMessage<CT>;
    using PrePrepare  = PrePrepareMessage<CT>;

public:

    ConsensusManager(Service & service,
                     Store & store,
                     Log & log,
                     const Config & config,
                     DelegateKeyStore & key_store,
                     MessageValidator & validator);

    void OnSendRequest(std::shared_ptr<Request> block,
                       logos::process_return & result);

    void OnRequestQueued();

    virtual void OnBenchmarkSendRequest(std::shared_ptr<Request> block,
                                        logos::process_return & result) = 0;

    void Send(const void * data, size_t size) override;

    virtual ~ConsensusManager() {}

    void OnRequestReady(std::shared_ptr<Request> block) override;

    void OnPrePrepare(const PrePrepare & block) override;

    std::shared_ptr<PrequelParser>
    BindIOChannel(std::shared_ptr<IOChannel>,
                  const DelegateIdentities &) override;

protected:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;

    static constexpr uint8_t DELIGATE_ID_MASK = 5;

    virtual void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) = 0;

    virtual bool Validate(std::shared_ptr<Request> block,
                          logos::process_return & result) = 0;

    virtual uint64_t GetStoredCount() = 0;

    void OnConsensusReached() override;
    void InitiateConsensus();

    virtual bool ReadyForConsensus();
    bool StateReadyForConsensus();

    void QueueRequest(std::shared_ptr<Request>);

    virtual void PrePreparePopFront() {};
    virtual void QueueRequestPrimary(std::shared_ptr<Request>) = 0;
    virtual void QueueRequestSecondary(std::shared_ptr<Request>);
    virtual PrePrepare & PrePrepareGetNext() = 0;
    virtual bool PrePrepareQueueEmpty() = 0;
    virtual bool PrePrepareQueueFull() = 0;
    virtual bool PrimaryContains(const logos::block_hash&) = 0;
    virtual bool SecondaryContains(const logos::block_hash&);

    bool IsPendingRequest(std::shared_ptr<Request>);

    bool IsPrePrepared(const logos::block_hash & hash);

    /// Request's primary delegate, 0 (delegate with most voting power) for Micro/Epoch Block
    /// @param request request
    /// @returns designated delegate
    virtual uint8_t DesignatedDelegate(std::shared_ptr<Request> request)
    {
        return 0;
    }

    virtual std::shared_ptr<ConsensusConnection<CT>> MakeConsensusConnection(
            std::shared_ptr<IOChannel>, const DelegateIdentities&) = 0;

    Connections                 _connections;
    DelegateKeyStore &          _key_store;
    MessageValidator &          _validator;
    std::mutex                  _connection_mutex;
    Log                         _log;
    uint8_t                     _delegate_id;
    SecondaryRequestHandler<CT> _secondary_handler;             ///< Secondary queue of blocks.
};

