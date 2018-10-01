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
class ConsensusManager : public PrimaryDelegate,
                         public ChannelBinder
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

    virtual void OnBenchmarkSendRequest(std::shared_ptr<Request> block,
                                        logos::process_return & result) = 0;

    void Send(const void * data, size_t size) override;

    virtual ~ConsensusManager() {}

    virtual
    std::shared_ptr<PrequelParser>
    BindIOChannel(std::shared_ptr<IOChannel>,
                  const DelegateIdentities &) override;

    PersistenceManager& get_persistence_manager() // ASK DEVON
    {
        return _persistence_manager;
    }

protected:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;

    virtual void ApplyUpdates(const PrePrepare &, uint8_t delegate_id) = 0;

    virtual bool Validate(std::shared_ptr<Request> block,
                          logos::process_return & result) = 0;

    virtual uint64_t GetStoredCount() = 0;

    void OnConsensusReached() override;
    void InitiateConsensus();

    virtual bool ReadyForConsensus();
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
    std::mutex         _connection_mutex;
    Log                _log;
    uint8_t            _delegate_id;
};

