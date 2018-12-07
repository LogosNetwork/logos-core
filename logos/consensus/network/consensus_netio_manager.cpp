/// @file
/// This file contains implementation of the ConsensusNetIOManager classes, which handle
/// network connections between the delegates
#include <logos/consensus/network/consensus_netio_manager.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/node/node.hpp>

const boost::posix_time::seconds ConsensusNetIOManager::HEARTBEAT{20};
const uint64_t ConsensusNetIOManager::GB_AGE = 20000; //milliseconds
const uint64_t ConsensusNetIOManager::MESSAGE_AGE = 60000; //milliseconds
const uint64_t ConsensusNetIOManager::MESSAGE_AGE_LIMIT = 100000; //milliseconds

using boost::asio::ip::make_address_v4;

ConsensusNetIOManager::ConsensusNetIOManager(Managers consensus_managers,
                                             Service & service, 
                                             logos::alarm & alarm, 
                                             const Config & config,
                                             DelegateKeyStore & key_store,
                                             MessageValidator & validator,
                                             PeerAcceptorStarter & starter,
                                             EpochInfo & epoch_info)
    : _service(service)
    , _delegates(config.delegates)
    , _consensus_managers(consensus_managers)
    , _alarm(alarm)
    , _key_store(key_store)
    , _validator(validator)
    , _delegate_id(config.delegate_id)
    , _epoch_info(epoch_info)
    , _heartbeat_timer(service)
    , _config(config)
{
    uint server_endpoints;

    auto local_endpoint(Endpoint(make_address_v4(config.local_address),
                        config.peer_port));

    _key_store.OnPublicKey(_delegate_id, _validator.GetPublicKey());

    for(auto & delegate : _delegates)
    {
        if(_delegate_id < delegate.id)
        {
            auto endpoint = Endpoint(make_address_v4(delegate.ip),
                                     local_endpoint.port());
            AddNetIOConnection(service, delegate.id, endpoint);
        }
        else if (delegate.id != _delegate_id)
        {
            ++server_endpoints;
        }
    }

    if(server_endpoints > 0)
    {
        starter.Start();
    }

    ScheduleTimer(HEARTBEAT);
}

ConsensusNetIOManager::~ConsensusNetIOManager()
{
    LOG_DEBUG(_log) << "~ConsensusNetIOManager, connections " << _connections.size()
                    << " connection " << TransitionConnectionToName(_epoch_info.GetConnection())
                    << " " << (int)DelegateIdentityManager::_global_delegate_idx;

    std::lock_guard<std::mutex> lock(_gb_mutex);

    for (auto it : _gb_collection)
    {
        it.netio->UnbindIOChannel();
        it.netio.reset();
    }
    _gb_collection.clear();
}

void
ConsensusNetIOManager::OnConnectionAccepted(
    const Endpoint endpoint,
    std::shared_ptr<Socket> socket,
    const ConnectedClientIds &ids)
{
    AddNetIOConnection(socket, ids.delegate_id, endpoint);
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

void
ConsensusNetIOManager::OnNetIOError(
    const Error &ec,
    uint8_t delegate_id,
    bool reconnect)
{
    // destruct delegate's ConsensusConnection for each consensus type
    {
        std::lock_guard<std::recursive_mutex> lock(_bind_mutex);

        for (auto &entry : _consensus_managers)
        {
            entry.second.OnNetIOError(delegate_id);
        }
    }

    // destruct/create delegate's netio instance
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    bool found = false;

    for (auto it = _connections.begin(); it != _connections.end(); ++it)
    {
        if ((*it)->IsRemoteDelegate(delegate_id))
        {
            auto netio(*it);

            LOG_ERROR(_log) << "ConsensusNetIOManager::OnNetIOError " << ec.message() << " " << (int)delegate_id
                            << " " << netio->GetEndpoint();

            _connections.erase(it);

            // if delegate is TCP/IP client then instantiate netio,
            // otherwise TCP/IP server is already accepting connections
            if (reconnect && _delegate_id < delegate_id)
            {
                auto endpoint = netio->GetEndpoint();
                _alarm.add(Seconds(ConsensusNetIO::CONNECT_RETRY_DELAY), [this, delegate_id, endpoint]() {
                    AddNetIOConnection(_service, delegate_id, endpoint);
                });
            }

            std::lock_guard<std::mutex> gblock(_gb_mutex);
            _gb_collection.push_back({GetStamp(), netio});
            found = true;
            break;
        }
    }

    if (!found)
    {
        LOG_ERROR(_log) << "ConsensusNetIOManager::OnNetIOError, delegate not found " << (int)delegate_id;
    }
}

template<typename T>
void
ConsensusNetIOManager::AddNetIOConnection(
    T &t,
    uint8_t remote_delegate_id,
    const Endpoint &endpoint)
{
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);

    auto bc = [this](std::shared_ptr<ConsensusNetIO> netio,
                     uint8_t id)
    {
        BindIOChannel(netio, id);
    };

    _connections.push_back(std::make_shared<ConsensusNetIO>(
            t, endpoint, _alarm, remote_delegate_id,
            _delegate_id, _key_store, _validator,
            bc, _connection_mutex, _epoch_info, *this));
}

void
ConsensusNetIOManager::ScheduleTimer(
    boost::posix_time::seconds sec)
{
    _heartbeat_timer.expires_from_now(sec);
    _heartbeat_timer.async_wait(std::bind(&ConsensusNetIOManager::OnTimeout, this,
                                std::placeholders::_1));
}

void
ConsensusNetIOManager::OnTimeout(
    const Error &ec)
{
    if (ec)
    {
        LOG_ERROR(_log) << "ConsensusNetIOManager::OnTimeout, error: " << ec.message();
        if (ec == boost::asio::error::operation_aborted)
        {
            return;
        }
    }

    using namespace boost::system::errc;
    HeartBeat heartbeat;
    vector<std::shared_ptr<ConsensusNetIO>> garbage;
    if (_config.heartbeat)
    {
        std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
        for (auto it : _connections) {
            if (it->Connected())
            {
                auto stamp = it->GetTimestamp();
                auto now = GetStamp();
                auto diff = now - stamp;
                if (diff > MESSAGE_AGE_LIMIT) {
                    LOG_DEBUG(_log) << "ConsensusNetIOManager::OnTimeout, scheduled for destruction "
                                    << (int) it->GetRemoteDelegateId() << " time diff " << diff;
                    garbage.push_back(it);
                } else if (diff > MESSAGE_AGE) {
                    LOG_DEBUG(_log) << "ConsensusNetIOManager::OnTimeout, sending heartbeat to "
                                    << (int) it->GetRemoteDelegateId();
                    it->Send(&heartbeat, sizeof(heartbeat));
                }
            }
        }
    }

    Error error(make_error_code(errc_t::io_error));
    for (auto it : garbage)
    {
        it->OnNetIOError(error, true);
    }

    auto now = GetStamp();

    std::lock_guard<std::mutex> lock(_gb_mutex);
    for (auto it = _gb_collection.begin(); it != _gb_collection.end();)
    {
        if (now - it->timestamp > GB_AGE)
        {
            auto netio = it->netio;
            LOG_DEBUG(_log) << "ConsensusNetIOManager::OnTimeout, gb collecting "
                            << (int)netio->GetRemoteDelegateId();
            it = _gb_collection.erase(it);
            netio->UnbindIOChannel();
            netio.reset();
        }
        else
        {
            ++it;
        }
    }

    ScheduleTimer(HEARTBEAT);
}

void
ConsensusNetIOManager::CleanUp()
{
    using namespace boost::system::errc;
    _heartbeat_timer.cancel();

    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);

    Error error(make_error_code(errc_t::io_error));
    while (_connections.size() != 0)
    {
        _connections[0]->OnNetIOError(error, false);
    }
}