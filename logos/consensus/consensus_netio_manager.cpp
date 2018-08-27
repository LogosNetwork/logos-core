//===-- logos/consensus/consensus_netio.hpp - ConsensusNetIOManager class implementation -------*- C++ -*-===//
//
// Open source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementation of the ConsensusNetIOManager classes, which handle
/// network connections between the delegates
///
//===----------------------------------------------------------------------===//
#include <logos/consensus/consensus_netio_manager.hpp>
#include <logos/node/node.hpp>

ConsensusNetIOManager::ConsensusNetIOManager(ConsensusManagers consensus_managers,
                                             Service & service, 
                                             logos::alarm & alarm, 
                                             const Config & config,
                                             DelegateKeyStore & key_store,
                                             MessageValidator & validator) 
    : _delegates(config.delegates)
    , _consensus_managers(consensus_managers)
    , _alarm(alarm)
    , _peer_acceptor(service, _log, Endpoint(boost::asio::ip::make_address_v4(config.local_address), 
        config.peer_port), this)
    , _key_store(key_store)
    , _validator(validator)
    , _delegate_id(config.delegate_id)
{
    std::set<Address> server_endpoints;

    auto local_endpoint(Endpoint(boost::asio::ip::make_address_v4(config.local_address),
                        config.peer_port));

    _key_store.OnPublicKey(_delegate_id, _validator.GetPublicKey());

    for(auto & delegate : _delegates)
    {
        auto endpoint = Endpoint(boost::asio::ip::make_address_v4(delegate.ip),
                                 local_endpoint.port());

        if(delegate.id == _delegate_id)
        {
            continue;
        }

        if(_delegate_id < delegate.id)
        {
            std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
            _connections.push_back(std::make_shared<ConsensusNetIO>(
                service, endpoint, _alarm,
                delegate.id, _delegate_id, _key_store, _validator,
                std::bind(&ConsensusNetIOManager::BindIOChannel, this, std::placeholders::_1, 
                    std::placeholders::_2), 
                _connection_mutex));
        }
        else
        {
            server_endpoints.insert(endpoint.address());
        }
    }

    if(server_endpoints.size())
    {
        _peer_acceptor.Start(server_endpoints);
    }
}

void 
ConsensusNetIOManager::OnConnectionAccepted(
    const Endpoint& endpoint, 
    std::shared_ptr<Socket> socket)
{
    auto entry = std::find_if(_delegates.begin(), _delegates.end(),
                              [&](const Config::Delegate & delegate){
                                  return delegate.ip == endpoint.address().to_string();
                              });

    // remote delegate id is piggy backed to the public key message and
    // is updated when the public key message is received
    uint8_t remote_delegate_id = (entry != _delegates.end()) ? entry->id : 0;

    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    _connections.push_back(std::make_shared<ConsensusNetIO>(socket, endpoint, _alarm, 
        remote_delegate_id, _delegate_id, _key_store, _validator,
        std::bind(&ConsensusNetIOManager::BindIOChannel, this, std::placeholders::_1, 
            std::placeholders::_2),
        _connection_mutex));
}

void 
ConsensusNetIOManager::BindIOChannel(
    std::shared_ptr<ConsensusNetIO> netio, 
    uint8_t remote_delegate_id)
{
    std::lock_guard<std::recursive_mutex> lock(_bind_mutex);

    BOOST_LOG(_log) << "ConsensusNetIO: Binding io channel of local " << (int)_delegate_id <<
        " to remote " << (int)remote_delegate_id;
    DelegateIdentities ids{_delegate_id, remote_delegate_id};
    for (auto it = _consensus_managers.begin(); it != _consensus_managers.end(); ++it)
    {
        auto consensus_connection = it->second.BindIOChannel(netio, ids);
        netio->AddConsensusConnection(it->first, consensus_connection);
    }
}