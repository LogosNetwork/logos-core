#pragma once

#include <logos/wallet_server/client/wallet_server_client.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/consensus_connection.hpp>
#include <logos/consensus/delegate_key_store.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/request_handler.hpp>

#include <boost/log/sources/record_ostream.hpp>

class IConsensusManager
{

public:

  IConsensusManager() {}

  virtual ~IConsensusManager() {}

  virtual
  std::shared_ptr<IConsensusConnection>
  BindIOChannel(std::shared_ptr<IIOChannel>,
                const DelegateIdentities &) = 0;
};

template<ConsensusType consensus_type>
class ConsensusManager : public PrimaryDelegate,
                         public IConsensusManager
{

protected:

    using Service     = boost::asio::io_service;
    using Config      = ConsensusManagerConfig;
    using Log         = boost::log::sources::logger_mt;
    using Connections = std::vector<std::shared_ptr<ConsensusConnection<consensus_type>>>;
    using Store       = logos::block_store;
    using Manager     = ConsensusManager<consensus_type>;
    using Request     = RequestMessage<consensus_type>;
    using PrePrepare  = PrePrepareMessage<consensus_type>;

public:

    ConsensusManager(Service & service,
                     Store & store,
                     logos::alarm & alarm,
                     Log & log,
                     const Config & config,
                     DelegateKeyStore & key_store,
                     MessageValidator & validator);

    void OnSendRequest(std::shared_ptr<Request> block, logos::process_return & result);

    virtual void OnBenchmarkSendRequest(std::shared_ptr<Request> block,
                                        logos::process_return & result) = 0;

    void Send(const void * data, size_t size) override;

    virtual ~ConsensusManager() {}

    virtual
    std::shared_ptr<IConsensusConnection> BindIOChannel(std::shared_ptr<IIOChannel>,
                                                        const DelegateIdentities &) override;

protected:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;

    virtual void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) = 0;

    virtual bool Validate(std::shared_ptr<Request> block, logos::process_return & result) = 0;

    void OnConsensusReached() override;
    virtual uint64_t OnConsensusReachedStoredCount() = 0;
    virtual bool OnConsensusReachedExt() = 0;
    void InitiateConsensus();

    bool ReadyForConsensus();
    virtual bool ReadyForConsensusExt() { return ReadyForConsensus(); }
    bool StateReadyForConsensus();

    virtual void PrePreparePopFront() {};

    virtual void QueueRequest(std::shared_ptr<Request>) = 0;
    virtual PrePrepare & PrePrepareGetNext() = 0;
    virtual bool PrePrepareQueueEmpty() = 0;
    virtual bool PrePrepareQueueFull() = 0;

    Connections        _connections;
    PersistenceManager _persistence_manager;
    DelegateKeyStore & _key_store;
    MessageValidator & _validator;
    logos::alarm &     _alarm;
    std::mutex         _connection_mutex;
    Log                _log;
    uint8_t            _delegate_id;
};

