#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/attempt.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>
#include <logos/bootstrap/pull.hpp>
#include <logos/lib/trace.hpp>


namespace Bootstrap
{
    BootstrapInitiator::BootstrapInitiator(logos::alarm & alarm,
               Store & store,
            logos::BlockCache & cache,
            PeerInfoProvider & peer_provider,
            uint8_t max_connected)
    : service(alarm.service)
    , alarm(alarm)
    , store(store)
    , cache(cache)
    , peer_provider(peer_provider)
    , stopped(false)
    , one_more(false)
    , max_connected(max_connected)
    {
        LOG_DEBUG(log) << "bootstrap_initiator::"<<__func__;
        thread = std::thread([this]() { run_bootstrap(); });
    }

    BootstrapInitiator::~BootstrapInitiator()
    {
        LOG_TRACE(log) << "bootstrap_initiator::"<<__func__;
        stop();
        thread.join();
    }

    void BootstrapInitiator::bootstrap()
    {
        LOG_DEBUG(log) << "bootstrap_initiator::"<<__func__;
        std::unique_lock<std::mutex> lock(mtx);
        if (stopped)
        {
            LOG_DEBUG(log) << "bootstrap_initiator::"<<__func__ << " already stopped";
            return;
        }

        if (attempt == nullptr) {
            attempt = std::make_shared<BootstrapAttempt>(alarm,
                    store,
                    cache,
                    peer_provider,
                    max_connected);
            condition.notify_all();
        }
        else
        {
            one_more = true;
        }
    }

    void BootstrapInitiator::bootstrap(logos::endpoint const &peer)
    {
        LOG_DEBUG(log) << "bootstrap_initiator::"<<__func__;
        //cannot add endpoint_a to peer list, since it could be
        //one of the delegate
        for(;;)
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (stopped)
            {
                LOG_DEBUG(log) << "bootstrap_initiator::"<<__func__ << " already stopped";
                return;
            }

            if(attempt == nullptr)
            {
                attempt = std::make_shared<BootstrapAttempt>(alarm,
                        store,
                        cache,
                        peer_provider,
                        max_connected);
                attempt->add_connection(peer);
                condition.notify_all();
                return;
            }
            else
            {
                if(attempt->add_connection(peer))
                {
                    one_more = true;
                    return;
                }
            }
        }
    }

    void BootstrapInitiator::run_bootstrap()
    {
        LOG_DEBUG(log) << "bootstrap_initiator::"<<__func__;
        std::unique_lock<std::mutex> lock(mtx);
        while (!stopped)
        {
            if (attempt != nullptr)
            {
                std::shared_ptr<BootstrapAttempt> attempt_ref = attempt;
                lock.unlock();
                attempt_ref->run();
                attempt_ref->stop();
                lock.lock();
                if( one_more )
                {
                    one_more = false;
                    LOG_DEBUG(log) << "bootstrap_initiator::"<<__func__<<" one more";
                    attempt = std::make_shared<BootstrapAttempt>(alarm,
                            store,
                            cache,
                            peer_provider,
                            max_connected);
                }
                else
                {
                    attempt = nullptr;
                }
            } else {
                LOG_TRACE(log) << "bootstrap_initiator::"<<__func__<<" before wait";
                condition.wait(lock);
                LOG_TRACE(log) << "bootstrap_initiator::"<<__func__<<" after wait";
            }
        }
    }

    bool BootstrapInitiator::check_progress()
    {
        LOG_TRACE(log) << "bootstrap_initiator::"<<__func__;
        std::lock_guard<std::mutex> lock(mtx);
        if(attempt == nullptr)
        {
            return false;
        }

#ifdef BOOTSTRAP_PROGRESS
        if(get_block_progress() == 0)//TODO how many blocks?
        {
            LOG_DEBUG(log) <<"bootstrap_initiator::check_progress calling attempt::stop";
            attempt->stop();
            attempt = nullptr;
        }
#endif
        return true;
    }

    void BootstrapInitiator::stop()
    {
        LOG_TRACE(log) << "bootstrap_initiator::"<<__func__;
        std::unique_lock<std::mutex> lock(mtx);
        stopped = true;
        if (attempt != nullptr) {
            attempt->stop();
            attempt = nullptr;
        }
        lock.unlock();
        condition.notify_all();
    }

    //////////////////////////////////////////////////////////////////////////////////

    boost::asio::ip::tcp::endpoint get_endpoint(std::string & address)
    {
        return boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(address),
                        BOOTSTRAP_PORT);
    }

    BootstrapListener::BootstrapListener(logos::alarm & alarm,
            Store & store,
            std::string & local_address,
            //uint16_t port_a,
            uint8_t max_accepted)
    : acceptor(alarm.service)
    , alarm(alarm)
    , local(get_endpoint(local_address))
    , service(alarm.service)
    , store(store)
    , max_accepted(max_accepted)
    {
        LOG_DEBUG(log) << "bootstrap_listener::"<<__func__
                       << " " << local.address().to_string()
                       << ":" << local.port();
    }

    BootstrapListener::~BootstrapListener()
    {
        LOG_TRACE(log) << "bootstrap_listener::"<<__func__;
        stop();
    }

    void BootstrapListener::start()
    {
        LOG_DEBUG(log) << "bootstrap_listener::"<<__func__;
        acceptor.open (local.protocol ());
        acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

        boost::system::error_code ec;
        acceptor.bind (local, ec);
        if (ec)
        {
            LOG_FATAL(log) << "Error while binding for bootstrap on port " << local.port()
                           << " " << ec.message();
            trace_and_halt();
        }

        acceptor.listen();
        accept_connection();
    }

    void BootstrapListener::stop()
    {
        LOG_DEBUG(log) << "bootstrap_listener::stop: acceptor->close";
        acceptor.close();
        std::unique_lock<std::mutex> lock(mtx);
        for (auto &con : connections)
        {
            LOG_DEBUG(log) << "bootstrap_listener::stop: socket->close";
            con->Disconnect();
        }
        while (!connections.empty())
        {
            condition.wait(lock);
        }
    }

    void BootstrapListener::accept_connection()
    {
        LOG_TRACE(log) << "bootstrap_listener::"<<__func__;
        auto socket(std::make_shared<BoostSocket>(service));
        acceptor.async_accept(*socket, [this, socket](boost::system::error_code const &ec)
        {
            accept_action(ec, socket);
        });
    }

    void BootstrapListener::accept_action(boost::system::error_code const &ec,
                                           std::shared_ptr<BoostSocket> socket_a)
    {
        LOG_DEBUG(log) << "bootstrap_listener::"<<__func__;
        accept_connection();
        if (!ec)
        {
            auto connection(std::make_shared<BootstrapServer>(*this, *socket_a, store));
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (connections.size() < max_accepted && acceptor.is_open())
                {
                    LOG_DEBUG(log) << "bootstrap_listener::accept_action: " << connections.size()
                                   << " acceptor.is_open(): " << acceptor.is_open();
                    connections.insert(connection);
                    connection->receive_request();
                } else {
                    LOG_WARN(log) << "bootstrap_listener::accept_action: " << connections.size()
                                       << " acceptor.is_open(): " << acceptor.is_open();
                }
            }
        } else {
            LOG_DEBUG(log) << "bootstrap_listener::accept_action: networking error: ec.message: "
                           << ec.message();
        }
    }

    void BootstrapListener::remove_connection(std::shared_ptr<BootstrapServer> server)
    {
        LOG_DEBUG(log) << "bootstrap_listener::"<<__func__;
        std::lock_guard <std::mutex> lock(mtx);
        connections.erase(server);
        condition.notify_all();
    }
}
