#pragma once

#include <rai/consensus/consensus_manager_config.hpp>
#include <rai/consensus/consensus_connection.hpp>
#include <rai/consensus/messages/messages.hpp>
#include <rai/consensus/primary_delegate.hpp>
#include <rai/consensus/peer_acceptor.hpp>
#include <rai/consensus/peer_manager.hpp>

#include <boost/log/sources/record_ostream.hpp>

class ConsensusManager : public PeerManager,
                         public PrimaryDelegate
{

    using Address          = boost::asio::ip::address;
	using Config           = ConsensusManagerConfig;
    using Log              = boost::log::sources::logger_mt;
    using ConnectionPolicy = std::less<boost::asio::ip::tcp::endpoint>;
    using Connections      = std::vector<std::shared_ptr<ConsensusConnection>>;

public:

	ConsensusManager(boost::asio::io_service & service,
	                 rai::alarm & alarm, Log & log,
					 const Config & config);

	void OnSendRequest(std::shared_ptr<rai::block> block);

    void OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<Socket> socket) override;

    void Send(void * data, size_t size) override;

    virtual ~ConsensusManager() {}

private:

    Connections  connections_;
	rai::alarm & alarm_;
	PeerAcceptor peer_acceptor_;
	Log          log_;
};

