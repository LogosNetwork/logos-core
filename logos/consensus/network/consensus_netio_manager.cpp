/// @file
/// This file contains implementation of the ConsensusNetIOManager classes, which handle
/// network connections between the delegates
#include <logos/consensus/network/consensus_netio_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/node/node.hpp>

using boost::asio::ip::make_address_v4;

ConsensusNetIOManager::ConsensusNetIOManager(Managers consensus_managers,
                                             Service & service, 
                                             logos::alarm & alarm, 
                                             const Config & config,
                                             DelegateKeyStore & key_store,
                                             MessageValidator & validator,
                                             PeerAcceptorStarter & starter,
                                             EpochInfo & epoch_info)
    : _delegates(config.delegates)
    , _consensus_managers(consensus_managers)
    , _alarm(alarm)
    , _key_store(key_store)
    , _validator(validator)
    , _delegate_id(config.delegate_id)
    , _epoch_info(epoch_info)
{
    std::set<Address> server_endpoints;

    auto local_endpoint(Endpoint(make_address_v4(config.local_address),
                        config.peer_port));

    _key_store.OnPublicKey(_delegate_id, _validator.GetPublicKey());

    for(auto & delegate : _delegates)
    {
        if(delegate.id == _delegate_id)
        {
            continue;
        }

        auto endpoint = Endpoint(make_address_v4(delegate.ip),
                                 local_endpoint.port());

        if(_delegate_id < delegate.id)
        {
            std::lock_guard<std::recursive_mutex> lock(_connection_mutex);

            auto bc = [this](std::shared_ptr<ConsensusNetIO> netio,
                             uint8_t id)
                      {
                          BindIOChannel(netio, id);
                      };

            _connections.push_back(
                std::make_shared<ConsensusNetIO>(
                        service, endpoint, _alarm, delegate.id,
                        _delegate_id, _key_store, _validator,
                        bc, _connection_mutex, _epoch_info));
        }
        else
        {
            server_endpoints.insert(endpoint.address());
        }
    }

    if(server_endpoints.size())
    {
        starter.Start(server_endpoints);
    }
}

ConsensusNetIOManager::~ConsensusNetIOManager()
{
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);

    BOOST_LOG(_log) << "~ConsensusNetIOManager, connections " << _connections.size()
                    << " connection " << TransitionConnectionToName(_epoch_info.GetConnection())
                    << " " << (int)NodeIdentityManager::_global_delegate_idx;

    for (auto conn : _connections)
    {
        conn->Close();
    }
}

void
ConsensusNetIOManager::OnConnectionAccepted(
    const Endpoint endpoint,
    std::shared_ptr<Socket> socket,
    const ConnectedClientIds &ids)
{
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);

    auto bc = [this](std::shared_ptr<ConsensusNetIO> netio,
                     uint8_t id)
              {
                  BindIOChannel(netio, id);
              };

    _connections.push_back(
            std::make_shared<ConsensusNetIO>(
                socket, endpoint, _alarm, ids.delegate_id,
                _delegate_id, _key_store, _validator,
                bc, _connection_mutex, _epoch_info));
}

void
ConsensusNetIOManager::BindIOChannel(
    std::shared_ptr<ConsensusNetIO> netio, 
    uint8_t remote_delegate_id)
{
    std::lock_guard<std::recursive_mutex> lock(_bind_mutex);

    DelegateIdentities ids{_delegate_id, remote_delegate_id};

    for (auto & entry : _consensus_managers)
    {
        netio->AddConsensusConnection(entry.first,
                                      entry.second.BindIOChannel(netio, ids));
    }
}