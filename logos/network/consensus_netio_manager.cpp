/// @file
/// This file contains implementation of the ConsensusNetIOManager classes, which handle
/// network connections between the delegates
#include <logos/network/consensus_netio_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/node/node.hpp>

const boost::posix_time::seconds ConsensusNetIOManager::HEARTBEAT{20};
const uint64_t ConsensusNetIOManager::MESSAGE_AGE = 60000; //milliseconds
const uint64_t ConsensusNetIOManager::MESSAGE_AGE_LIMIT = 100000; //milliseconds

using boost::asio::ip::make_address_v4;

ConsensusNetIOManager::ConsensusNetIOManager(std::shared_ptr<NetIOHandler> request_manager,
                                             std::shared_ptr<NetIOHandler> micro_manager,
                                             std::shared_ptr<NetIOHandler> epoch_manager,
                                             Service & service, 
                                             logos::alarm & alarm, 
                                             const Config & config,
                                             DelegateKeyStore & key_store,
                                             MessageValidator & validator,
                                             PeerAcceptorStarter & starter)
    : _service(service)
    , _consensus_managers({
            {ConsensusType::Request, request_manager},
            {ConsensusType::MicroBlock, micro_manager},
            {ConsensusType::Epoch, epoch_manager}
        })
    , _alarm(alarm)
    , _key_store(key_store)
    , _validator(validator)
    , _delegate_id(config.delegate_id)
    , _heartbeat_timer(service)
    , _config(config)
    , _acceptor(starter)
{
    _key_store.OnPublicKey(_delegate_id, _validator.GetPublicKey());
}

void
ConsensusNetIOManager::Start(std::shared_ptr<EpochInfo> epoch_info)
{
    _epoch_info = epoch_info;

    if(_delegate_id != 0)
    {
        _acceptor.Start();
    }

    ScheduleTimer(HEARTBEAT);
}

void
ConsensusNetIOManager::AddDelegate(uint8_t delegate_id, std::string &ip, uint16_t port)
{
    if (std::find_if(_delegates.begin(), _delegates.end(), [&](auto delegate){
            return delegate.id == delegate_id;}) != _delegates.end())
    {
        LOG_DEBUG(_log) << "ConsensusNetIOManager::AddDelegate, delegate id " << (int) delegate_id
                        << " is already connected ";
        return;
    }


    _delegates.push_back({ip, delegate_id});

    if (_delegate_id < delegate_id)
    {
        auto endpoint = Endpoint(make_address_v4(ip), port);
        AddNetIOConnection(_service, delegate_id, endpoint);
    }
    else if (delegate_id != _delegate_id)
    {
        _acceptor.Start();
    }
}

ConsensusNetIOManager::~ConsensusNetIOManager()
{
    auto info = GetSharedPtr(_epoch_info, "ConsensusNetIOManager::~ConsensusNetIOManager, object destroyed");
    if (!info)
    {
        return;
    }
    LOG_DEBUG(_log) << "~ConsensusNetIOManager, connections " << _connections.size()
                    << " connection " << (info?TransitionConnectionToName(info->GetConnection()):" not available")
                    << " " << (int)DelegateIdentityManager::GetGlobalDelegateIdx();
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
                                      entry.second->BindIOChannel(netio, ids));
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
            entry.second->OnNetIOError(delegate_id);
        }
    }

    // destruct/create delegate's netio instance
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
    bool found = false;

    for (auto it = _connections.begin(); it != _connections.end(); ++it)
    {
        if ((*it)->IsRemoteDelegate(delegate_id))
        {
            LOG_ERROR(_log) << "ConsensusNetIOManager::OnNetIOError " << ec.message() << " " << (int)delegate_id
                            << " " << (*it)->GetEndpoint();

            auto endpoint = (*it)->GetEndpoint();
            (*it)->UnbindIOChannel();
            (*it).reset();

            _connections.erase(it);

            // if delegate is TCP/IP client then instantiate netio,
            // otherwise TCP/IP server is already accepting connections
            if (reconnect && _delegate_id < delegate_id)
            {
                std::weak_ptr<ConsensusNetIOManager> this_w = shared_from_this();
                _alarm.add(Seconds(ConsensusNetIO::CONNECT_RETRY_DELAY), [this_w, delegate_id, endpoint]() {
                    auto this_s = GetSharedPtr(this_w, "ConsensusNetIOManager::OnNetIOError, object destroyed");
                    if (!this_s)
                    {
                        return;
                    }
                    this_s->AddNetIOConnection(this_s->_service, delegate_id, endpoint);
                });
            }

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

    auto bc = [this](std::shared_ptr<ConsensusNetIO> netio,
                     uint8_t id)
    {
        BindIOChannel(netio, id);
    };

    using PtrMemberFun = void (ConsensusNetIO::*)(); // ConsensusNetIO member function pointer

    PtrMemberFun cb;
    auto info = GetSharedPtr(_epoch_info, "ConsensusNetIOManager::AddNetIOConnection, object destroyed");
    if (!info)
    {
        return;
    }
    auto netio = std::make_shared<ConsensusNetIO>(
            t, endpoint, _alarm, remote_delegate_id,
            _delegate_id, _key_store, _validator,
            bc, _connection_mutex, info, *this, cb);
    {
        std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
        _connections.push_back(netio);
    }
    ((*netio).*cb)();

}

void
ConsensusNetIOManager::ScheduleTimer(
    boost::posix_time::seconds sec)
{
    std::weak_ptr<ConsensusNetIOManager> this_w = shared_from_this();
    _heartbeat_timer.expires_from_now(sec);
    _heartbeat_timer.async_wait([this_w](const Error &ec) {
        auto this_s = GetSharedPtr(this_w, "ConsensusNetIOManager::ScheduleTimer, object destroyed");
        if (!this_s)
        {
            return;
        }
        this_s->OnTimeout(ec);
    });
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

                    std::vector<uint8_t> buf;
                    heartbeat.Serialize(buf);
                    it->Send(buf.data(), buf.size());
                }
            }
        }
    }

    Error error(make_error_code(errc_t::io_error));
    for (auto it : garbage)
    {
        it->OnNetIOError(error, true);
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
    Connections connections = _connections;
    for (auto it : connections)
    {
        it->OnNetIOError(error, false);
    }
    connections.clear();
}

bool
ConsensusNetIOManager::AddToConsensusQueue(const uint8_t * data,
                                           uint8_t version,
                                           MessageType message_type,
                                           ConsensusType consensus_type,
                                           uint32_t payload_size,
                                           uint8_t delegate_id)
{
    std::lock_guard<std::recursive_mutex> lock(_connection_mutex);

    for (auto it = _connections.begin(); it != _connections.end(); ++it)
    {
        if ((*it)->GetRemoteDelegateId() == delegate_id)
        {
            (*it)->Push(data, version, message_type, consensus_type, payload_size, true);
            break;
        }
    }
    return true;
}
