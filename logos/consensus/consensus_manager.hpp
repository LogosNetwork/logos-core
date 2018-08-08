#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/consensus/consensus_connection.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/consensus/primary_delegate.hpp>
#include <logos/consensus/request_handler.hpp>
#include <logos/consensus/peer_acceptor.hpp>
#include <logos/consensus/peer_manager.hpp>

#include <boost/log/sources/record_ostream.hpp>

#include "delegate_key_store.hpp"

class ConsensusManager : public PeerManager,
                         public PrimaryDelegate
{

    using Service          = boost::asio::io_service;
    using Address          = boost::asio::ip::address;
	using Config           = ConsensusManagerConfig;
    using Log              = boost::log::sources::logger_mt;
    using ConnectionPolicy = std::less<boost::asio::ip::tcp::endpoint>;
    using Connections      = std::vector<std::shared_ptr<ConsensusConnection>>;
    using Store            = logos::block_store;
    using BlockBuffer      = std::list<std::shared_ptr<logos::state_block>>;
    using Delegates        = std::vector<std::string>;

public:

	ConsensusManager(Service & service,
	                 Store & store,
	                 logos::alarm & alarm,
	                 Log & log,
					 const Config & config);

	void OnSendRequest(std::shared_ptr<logos::state_block> block, logos::process_return & result);
	void OnBenchmarkSendRequest(std::shared_ptr<logos::state_block> block, logos::process_return & result);

    void OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket> socket) override;

    void Send(const void * data, size_t size) override;

    virtual ~ConsensusManager() {}

    void BufferComplete(logos::process_return & result);

private:

    static constexpr uint8_t BATCH_TIMEOUT_DELAY = 15;

    bool Validate(std::shared_ptr<logos::state_block> block, logos::process_return & result);

    void OnConsensusReached() override;
    void InitiateConsensus();

    bool ReadyForConsensus();
    bool StateReadyForConsensus();

    void SendBufferedBlocks();

    void EstablishDelegateIds(const std::string & local_address);
    uint8_t GetDelegateId(const std::string & address);

    Connections        _connections;
    Delegates          _delegates;
    RequestHandler     _handler;
    PersistenceManager _persistence_manager;
    DelegateKeyStore   _key_store;
    MessageValidator   _validator;
	logos::alarm &       _alarm;
	PeerAcceptor       _peer_acceptor;
	BlockBuffer        _buffer;
    std::mutex         _connection_mutex;
	Log                _log;
    uint8_t            _delegate_id;
	bool               _using_buffered_blocks = false;
};

