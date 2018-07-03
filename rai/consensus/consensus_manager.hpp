#pragma once

#include <rai/consensus/persistence/persistence_manager.hpp>
#include <rai/consensus/consensus_manager_config.hpp>
#include <rai/consensus/consensus_connection.hpp>
#include <rai/consensus/request_handler.hpp>
#include <rai/consensus/messages/messages.hpp>
#include <rai/consensus/primary_delegate.hpp>
#include <rai/consensus/request_handler.hpp>
#include <rai/consensus/peer_acceptor.hpp>
#include <rai/consensus/peer_manager.hpp>

#include <boost/log/sources/record_ostream.hpp>

class ConsensusManager : public PeerManager,
                         public PrimaryDelegate
{

    using Service          = boost::asio::io_service;
    using Address          = boost::asio::ip::address;
	using Config           = ConsensusManagerConfig;
    using Log              = boost::log::sources::logger_mt;
    using ConnectionPolicy = std::less<boost::asio::ip::tcp::endpoint>;
    using Connections      = std::vector<std::shared_ptr<ConsensusConnection>>;
    using Store            = rai::block_store;

public:

	ConsensusManager(Service & service,
	                 Store & store,
	                 rai::alarm & alarm,
	                 Log & log,
					 const Config & config);

	void OnSendRequest(std::shared_ptr<rai::state_block> block, rai::process_return & result);

    void OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket> socket) override;

    void Send(void * data, size_t size) override;

    virtual ~ConsensusManager() {}

private:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;

    bool Validate(std::shared_ptr<rai::state_block> block, rai::process_return & result);

    void OnConsensusReached() override;
    void InitiateConsensus();
    void OnBatchTimeout();

    bool ReadyForConsensus();

    void ScheduleBatchTimeout();
    void CancelBatchTimeout();

    Connections        _connections;
    RequestHandler     _handler;
    PersistenceManager _persistence_manager;
    rai::keypair       _keypair;
	rai::alarm &       _alarm;
	PeerAcceptor       _peer_acceptor;
	Log                _log;
	uint64_t           _batch_timeout_handle;
	bool               _batch_timeout_scheduled = false;
};

