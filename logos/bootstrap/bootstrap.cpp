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
    logos::endpoint
    BootstrapInitiator::bad_address(boost::asio::ip::make_address("0.0.0.0"), 0);

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

    void BootstrapInitiator::bootstrap(logos_global::BootstrapCompleteCB cb,
                                       logos::endpoint const & peer)
    {
        LOG_DEBUG(log) << "bootstrap_initiator::"<<__func__;
        std::unique_lock<std::mutex> lock(mtx);

#ifdef BOOTSTRAP_INITIATOR_DEBUG
        static uint call_count = 0;
        LOG_TRACE(log) << "bootstrap_initiator::"<<__func__ << " call_count=" << call_count;
#endif
        if (stopped)
        {
            LOG_WARN(log) << "bootstrap_initiator::"<<__func__ << " already stopped";
            lock.unlock();
            if(cb)
                cb(logos_global::BootstrapResult::BootstrapInitiatorStopped);
            return;
        }

        if(cb)
        {
            cbq.push_back(cb);
        }

#ifdef BOOTSTRAP_INITIATOR_DEBUG
        auto cc = call_count++;
        auto self_cb = [this, cc](logos_global::BootstrapResult res){
            LOG_DEBUG(log) << "bootstrap_initiator::bootstrap"
                           << " call_count=" << cc
                           << " callback res=" << logos_global::BootstrapResultToString(res);
        };
        cbq.push_back(self_cb);
#endif

        if (attempt == nullptr)
        {
            one_more = true;
            attempt = std::make_shared<BootstrapAttempt>(alarm,
                    store,
                    cache,
                    peer_provider,
                    *this,
                    max_connected);
            condition.notify_all();
        }

        //cannot add endpoint_a to peer list, since it could be one of the delegate
        if (peer != bad_address)
            attempt->add_connection(peer);
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
                            *this,
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
        std::unique_lock<std::mutex> lock(mtx);
        if(attempt == nullptr)
        {
            return false;
        }

#ifdef BOOTSTRAP_PROGRESS
        if(get_block_progress() == 0)//TODO how many blocks?
        {
            LOG_DEBUG(log) <<"bootstrap_initiator::check_progress calling attempt::stop";
            auto attempt_ref = attempt;
            attempt = nullptr;
            lock.unlock();
            attempt_ref->stop();
            notify(logos_global::BootstrapResult::Incomplete);
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

    void BootstrapInitiator::notify(logos_global::BootstrapResult res)
    {
        LOG_INFO(log) << "bootstrap_initiator::notify, result="
                      << logos_global::BootstrapResultToString(res);

        std::unique_lock<std::mutex> lock(mtx);
        one_more = false;
        auto to_call(std::move(cbq));
        attempt = nullptr;
        lock.unlock();

        LOG_TRACE(log) << "bootstrap_initiator::notify, # of callback=" << to_call.size();
        for(auto & c : to_call)
        {
            service.post([this, c, res]()
            {
                LOG_TRACE(log) << "bootstrap_initiator::notify, calling ";
                c(res);
            });
        }
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
