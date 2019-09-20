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
                                             PeerAcceptorStarter & starter)
    : _service(service)
    , _consensus_managers({
            {ConsensusType::Request, request_manager},
            {ConsensusType::MicroBlock, micro_manager},
            {ConsensusType::Epoch, epoch_manager}
        })
    , _alarm(alarm)
    , _delegate_id(config.delegate_id)
    , _heartbeat_timer(service)
    , _config(config)
    , _acceptor(starter)
    , _startup_timer(service)
{

    for(auto& d: config.delegates)
    {
        LOG_INFO(_log) << "ConsensusNetIOManager::ConsensusNetIOManager-"
            << "delegate =" << (int)d.id << " is in config"
            << ",ip = " << d.ip;
        _delegates[d.id] = d;
    }
}

void
ConsensusNetIOManager::Start(std::shared_ptr<EpochInfo> epoch_info)
{

    uint8_t num_delegates = epoch_info->GetNumDelegates();
    _epoch_info = epoch_info;

    LOG_INFO(_log) << "ConsensusNetIOManager::Start - "
        << "epoch num = " << epoch_info->GetEpochNumber()
        << ", _delegate_id = " << unsigned(_delegate_id)
        << ",num_delegates = " << unsigned(num_delegates);

    for(uint8_t i = 0; i < num_delegates; ++i)
    {
        if(i == _delegate_id) continue;
        DelegateIdentities ids{_delegate_id, i};

        auto netio = AddNetIOConnection(i);
        for(auto & entry : _consensus_managers)
        {
            auto backup = entry.second->AddBackupDelegate(ids);
            netio->AddConsensusConnection(entry.first,backup);
        }
        //If delegate is in config, and id is greater than ours, connect now as client
        if(_delegate_id < i)
        {
            auto iter = _delegates.find(i);
            if(iter != _delegates.end())
            {
                Endpoint endpoint(make_address_v4(iter->second.ip),_config.peer_port);
                netio->BindEndpoint(endpoint);
                netio->Connect();
                LOG_INFO(_log) << "ConsensusNetIOManager::Start - delegate="
                    << unsigned(iter->second.id)
                    << ",epoch_number = " << epoch_info->GetEpochNumber()
                    << "Connecting now.";
            }
        }
    }

    if(_delegate_id != 0)
    {
        _acceptor.Start();
    }

    ScheduleTimer(HEARTBEAT);

    //if we don't connect in 5 minutes, start p2p consensus
    boost::posix_time::seconds startup_timeout{300};
    _startup_timer.expires_from_now(startup_timeout);
    //if not genesis, only wait 30 seconds before starting p2p consensus
    if(GetEpochNumber() > GENESIS_EPOCH+1)
    {
        startup_timeout = boost::posix_time::seconds(30);
    }

    std::weak_ptr<ConsensusNetIOManager> this_w = shared_from_this();
    auto this_s = GetSharedPtr(this_w,
            "ConsensusNetIOManager::_startup_timer, object destroyed");

    _startup_timer.async_wait([this_s](const Error &ec) {

        if (ec)
        {
            LOG_ERROR(this_s->_log)
                << "ConsensusNetIOManager::_startup_timer, error: "
                << ec.message();
            if (ec == boost::asio::error::operation_aborted)
            {
                return;
            }
        }
        for(auto & entry : this_s->_consensus_managers)
        {
            if(entry.first == ConsensusType::Request)
            {
                std::shared_ptr<RequestConsensusManager> mgr =
                    static_pointer_cast<RequestConsensusManager>(entry.second);
                //StartConsensusWithP2p calls OnDelegatesConnected(), which
                //must execute serially. OnDelegatesConnected() is always
                //called while holding _bind_mutex.
                //Furthermore, DelegatesConnected() reads a variable that is
                //updated by multiple threads
                std::lock_guard<std::recursive_mutex> lock(this_s->_bind_mutex);
                if(!mgr->DelegatesConnected())
                {
                    LOG_INFO(this_s->_log)
                        << "ConsensusNetIOManager::_startup_timer - "
                        << "Delegates not connected, starting p2p consensus";
                    mgr->StartConsensusWithP2p();
                }
                else
                {
                    LOG_INFO(this_s->_log) << "ConsensusNetIOManager::_startup_timer"
                        << "-delegates connected";
                }
            }
        }

    });


}

void
ConsensusNetIOManager::AddDelegate(uint8_t delegate_id, std::string &ip, uint16_t port)
{
    LOG_INFO(_log) << "ConsensusNetIOManager::AddDelegate - "
        << "delegate.id=" << unsigned(delegate_id)
        << ",_delegate_id=" << unsigned(_delegate_id)
        << ",epoch_number=" << GetEpochNumber()
        << ",remote ip=" << ip <<",port=" << port;
    auto iter = _delegates.find(delegate_id);
    if(iter != _delegates.end())
    {
        LOG_DEBUG(_log) << "ConsensusNetIOManager::AddDelegate, delegate id "
            << (int) delegate_id
            << " is already connected "
            << ", epoch_number=" << GetEpochNumber();
        if(ip != iter->second.ip)
        {
            LOG_WARN(_log) << "ConsensusNetIOManager::AddDelegate-"
                << "ips do not match. stored ip=" << iter->second.ip
                << ",received ip=" << ip;
        }
        return;
    }

    _delegates[delegate_id] = Config::Delegate(ip,delegate_id);

    if (_delegate_id < delegate_id)
    {
        auto endpoint = Endpoint(make_address_v4(ip), port);
        for(auto & connection : _connections)
        {
            if(connection->GetRemoteDelegateId() == delegate_id)
            {
                connection->BindEndpoint(endpoint);
                connection->Connect();
                LOG_INFO(_log) << "ConsensusNetIOManager::AddDelegate - "
                    << "added endpoint for delegate " << unsigned(delegate_id)
                    << ", _delegate_id=" << unsigned(_delegate_id)
                    << ", epoch_number=" << GetEpochNumber();
                return;
            }
        }
        LOG_FATAL(_log) << "ConsensusNetIOManager::AddDelegate - "
            << "failed to find proper connection to bind endpoint - "
            << "delegate = " << unsigned(delegate_id);
        trace_and_halt();
    }
    else
    {
        LOG_WARN(_log) << "ConsensusNetIOManager::AddDelegate - "
            << "AddDelegate called for delegate with id less than our own"
            << " delegate = " << unsigned(delegate_id)
            << ", _delegate_id = " << unsigned(_delegate_id)
            << ", epoch_number="<< GetEpochNumber();
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
                    << " connection " << (info?TransitionConnectionToName(info->GetConnection()):" not available");
}

void
ConsensusNetIOManager::OnConnectionAccepted(
    const Endpoint endpoint,
    std::shared_ptr<Socket> socket,
    uint8_t delegate_id)
{
    LOG_INFO(_log) << "ConsensusNetIOManager::OnConnectionAccepted - "
        << "accepted connection from delegate " << unsigned(delegate_id)
        << ",epoch_number=" << GetEpochNumber();
    for(auto & connection : _connections)
    {
        if(connection->GetRemoteDelegateId() == delegate_id)
        {
            LOG_INFO(_log) << "ConsensusNetIOManager::OnConnectionAccepted -"
                << "found proper netio for delegate = " << unsigned(delegate_id);

            //Bind the socket to existing net io
            connection->BindEndpoint(endpoint);
            connection->BindSocket(socket);
            connection->OnConnect();
            return;
        }
    }
    LOG_FATAL(_log) << "ConsensusNetIOManager::OnConnectionAccepted - "
        << "failed to find proper ConsensusNetIO to bind socket - "
        << "delegate_id = " << unsigned(delegate_id);
    trace_and_halt();
}

uint32_t ConsensusNetIOManager::GetEpochNumber()
{
    auto epoch_info = _epoch_info.lock();
    uint32_t epoch_number = 0;
    if(epoch_info)
    {
        epoch_number = epoch_info->GetEpochNumber();
    }
    return epoch_number;
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
        LOG_INFO(_log) << "ConsensusNetIOManager::BindIOChannel - "
            << "Binding io channel for " << ConsensusToName(entry.first)
            << " for remote delegate " << unsigned(remote_delegate_id);
        entry.second->BindIOChannel(netio,ids);
    }
}

bool
ConsensusNetIOManager::CanReachQuorumViaDirectConnect()
{
    //only need to check with one manager, since they all share the same IOChannel 
    return _consensus_managers[ConsensusType::Request]->CanReachQuorumViaDirectConnect();
}

void
ConsensusNetIOManager::EnableP2p(bool enable)
{
    //When connection fails, enable p2p for the time being
    //this enables p2p for the primary
    for(auto & netio_handler : _consensus_managers)
    {
        auto CT = netio_handler.first;
        if(CT == ConsensusType::Request)
        {

            auto mgr = static_pointer_cast<ConsensusManager<ConsensusType::Request>>(netio_handler.second);

            mgr->EnableP2p(enable);
        }
        else if (CT == ConsensusType::MicroBlock)
        {
            auto mgr = static_pointer_cast<ConsensusManager<ConsensusType::MicroBlock>>(netio_handler.second);

            mgr->EnableP2p(enable);
        }
        else
        {
            auto mgr = static_pointer_cast<ConsensusManager<ConsensusType::Epoch>>(netio_handler.second);

            mgr->EnableP2p(enable);

        }
    }

}

std::shared_ptr<ConsensusNetIO>
ConsensusNetIOManager::AddNetIOConnection(
    uint8_t remote_delegate_id)
{
    LOG_INFO(_log) << "ConsensusNetIOManager::AddNetIOConnection - "
        << "adding connection for delegate = " << unsigned(remote_delegate_id);

    auto bc = [this](std::shared_ptr<ConsensusNetIO> netio,
                     uint8_t id)
    {
        BindIOChannel(netio, id);
    };

    auto info = GetSharedPtr(_epoch_info, "ConsensusNetIOManager::AddNetIOConnection, object destroyed");
    if (!info)
    {
        return nullptr;
    }

   bool pending = remote_delegate_id < _delegate_id; 
    auto netio = std::make_shared<ConsensusNetIO>(
            _service, _alarm, remote_delegate_id,
            _delegate_id, bc, info, *this, pending);
    //DelegateMap is used to treat a Post_Committed block as a post-commit
    DelegateMap::GetInstance()->AddSink(info->GetEpochNumber(), remote_delegate_id, netio);

    {
        std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
        _connections.push_back(netio);
    }


    return netio;
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
    LOG_INFO(_log) << "ConsensusNetIOManager::OnTimeout";
    if (ec)
    {
        LOG_ERROR(_log) << "ConsensusNetIOManager::OnTimeout, error: " << ec.message();
        if (ec == boost::asio::error::operation_aborted)
        {
            return;
        }
    }

    if (_config.heartbeat)
    {
        LOG_INFO(_log) << "ConsensusNetIOManager::OnTimeout-"
            << "sending heartbeats";
        std::lock_guard<std::recursive_mutex> lock(_connection_mutex);
        for (auto it : _connections)
        {
            it->CheckHeartbeat();
        }
    }

    ScheduleTimer(HEARTBEAT);
}

void
ConsensusNetIOManager::CleanUp()
{
    _startup_timer.cancel();

    //TODO - do we need to lock both mutexs here?
    std::lock_guard<std::recursive_mutex> lock(_bind_mutex);
    using namespace boost::system::errc;
    _heartbeat_timer.cancel();
    LOG_INFO(_log) << "ConsensusNetIOManager::CleanUp()";

    std::lock_guard<std::recursive_mutex> lock2(_connection_mutex);

    Error error(make_error_code(errc_t::io_error));
    for (auto it : _connections)
    {
        it->OnNetIOError(error, false);
        it->UnbindIOChannel();
    }
    _connections.clear();

    // destruct delegate's ConsensusConnection for each consensus type
    for (auto &entry : _consensus_managers)
    {
        entry.second->DestroyAllBackups();
    }

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
    LOG_INFO(_log) << "ConsensusNetIOManager::AddToConsensusQueue - "
        << "Receivied msg_type = " << MessageToName(message_type)
        << " - consensus_type = " << ConsensusToName(consensus_type)
        << " - delegate_id = " << unsigned(delegate_id);

    for (auto it = _connections.begin(); it != _connections.end(); ++it)
    {
        if ((*it)->GetRemoteDelegateId() == delegate_id)
        {
            LOG_INFO(_log) << "ConsensusNetIOManager::AddToConsensusQueue - "
                << "found correct backup!";
            (*it)->Push(data, version, message_type, consensus_type, payload_size, true);
            break;
        }
    }
    return true;
}
