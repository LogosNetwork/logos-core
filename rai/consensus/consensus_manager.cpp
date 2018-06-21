#include <rai/consensus/consensus_manager.hpp>

#include <rai/node/node.hpp>

ConsensusManager::ConsensusManager(boost::asio::io_service & service,
                                   rai::alarm & alarm,
                                   Log & log,
                                   const Config & config)
    : PrimaryDelegate(log)
    , alarm_(alarm)
    , peer_acceptor_(service, log,
                     Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                              config.peer_port),
                     this)
{
    std::set<Address> server_endpoints;

    auto local_endpoint(Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                        config.peer_port));

    for(const auto & peer : config.stream_peers)
    {
        auto endpoint = Endpoint(boost::asio::ip::make_address_v4(peer),
                                 local_endpoint.port());

        if(endpoint == local_endpoint)
        {
            continue;
        }

        if(ConnectionPolicy()(local_endpoint, endpoint))
        {
            connections_.push_back(
                    std::make_shared<ConsensusConnection>(service, alarm, log_,
                                                          endpoint, this));
        }
        else
        {
            server_endpoints.insert(endpoint.address());
        }
    }

    peer_acceptor_.Start(server_endpoints);


    // Testing
    alarm_.add(std::chrono::steady_clock::now() + std::chrono::seconds(40),
              [this](){ this->OnSendRequest(nullptr); });
}

void ConsensusManager::OnSendRequest(std::shared_ptr<rai::block> block)
{
    state_ = ConsensusState::PRE_PREPARE;

    BOOST_LOG (log_) << "ConsensusManager - Send request initiated";

    PrePrepareMessage msg;

    for(auto conn : connections_)
    {
        conn->Send(msg);
    }


    // Testing
    alarm_.add(std::chrono::steady_clock::now() + std::chrono::seconds(15),
              [this](){ this->OnSendRequest(nullptr); });
}

void ConsensusManager::OnConnectionAccepted(const Endpoint& endpoint, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    connections_.push_back(std::make_shared<ConsensusConnection>(socket, alarm_, log_, endpoint, this));
}

void ConsensusManager::Send(void * data, size_t size)
{
    for(auto conn : connections_)
    {
        conn->Send(data, size);
    }
}

