#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/consensus_connection.hpp>
#include <logos/consensus/batchblock_consensus_connection.hpp>
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
  virtual std::shared_ptr<IConsensusConnection> BindIOChannel(std::shared_ptr<IIOChannel>, const DelegateIdentities &) = 0;
};

template<ConsensusType consensus_type>
class ConsensusManager : public PrimaryDelegate,
                         public IConsensusManager
{

protected:

    using Service     = boost::asio::io_service;
	using Config      = ConsensusManagerConfig;
    using Log         = boost::log::sources::logger_mt;
    using Connections = std::vector<std::shared_ptr<IConsensusConnectionOutChannel>>;
    using Store       = logos::block_store;

public:

	ConsensusManager(Service & service,
	                 Store & store,
	                 logos::alarm & alarm,
	                 Log & log,
					 const Config & config,
                   DelegateKeyStore & key_store,
                   MessageValidator & validator);

	void OnSendRequest(std::shared_ptr<RequestMessage<consensus_type>> block, logos::process_return & result);
	virtual void OnBenchmarkSendRequest(std::shared_ptr<RequestMessage<consensus_type>> block, logos::process_return & result) = 0;

    void Send(const void * data, size_t size) override;

    virtual ~ConsensusManager() {}

    virtual std::shared_ptr<IConsensusConnection> BindIOChannel(std::shared_ptr<IIOChannel>, const DelegateIdentities &) override;

protected:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;

	  virtual void ApplyUpdates(const PrePrepareMessage<consensus_type> &, uint8_t delegate_id) = 0;

    virtual bool Validate(std::shared_ptr<RequestMessage<consensus_type>> block, logos::process_return & result) = 0;

    void OnConsensusReached() override;
    virtual uint64_t OnConsensusReached_StoredCount() = 0;
    virtual bool OnConsensusReached_Ext() = 0;
    void InitiateConsensus();

    bool ReadyForConsensus();
    virtual bool ReadyForConsensus_Ext() { return ReadyForConsensus(); }
    bool StateReadyForConsensus();

    virtual void QueueRequest(std::shared_ptr<RequestMessage<consensus_type>>) = 0;
    virtual PrePrepareMessage<consensus_type> & PrePrepare_GetNext() = 0;
    virtual void PrePrepare_PopFront() = 0;
    virtual bool PrePrepare_QueueEmpty() = 0;
    virtual bool PrePrepare_QueueFull() = 0;

    Connections        _connections;
    PersistenceManager _persistence_manager;
    DelegateKeyStore & _key_store;
    MessageValidator & _validator;
	logos::alarm &       _alarm;
    std::mutex         _connection_mutex;
	Log                _log;
    uint8_t            _delegate_id;
};

