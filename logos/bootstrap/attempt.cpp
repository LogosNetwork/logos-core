#include <logos/bootstrap/attempt.hpp>
#include <logos/bootstrap/pull_connection.hpp>

namespace Bootstrap
{
    bootstrap_attempt::bootstrap_attempt (logos::alarm & alarm,
            Store & store,
            BlockCache & cache,
            PeerInfoProvider &peer_provider,
            uint8_t max_connected)
    : alarm(alarm)
    , store(store)
    , peer_provider(peer_provider)
    , connections(0)
    , max_connected(max_connected)
    , session_id(PeerInfoProvider::GET_PEER_NEW_SESSION)
    , puller(cache, store)
    , stopped(false)
    {
        LOG_DEBUG(log) << "Starting bootstrap attempt";
    }

    bootstrap_attempt::~bootstrap_attempt()
    {
        LOG_INFO(log) << "Exiting bootstrap attempt";
        stop();
        if(session_id != PeerInfoProvider::GET_PEER_NEW_SESSION)
            peer_provider.close_session(session_id);
    }

    bool bootstrap_attempt::request_tips(std::unique_lock<std::mutex> &lock_a)
    {
        // NOTE: Get the connection from the pool and get tips.
        auto failed(true);
        auto connection_l(connection(lock_a));
        if (connection_l)
        {
            std::future<bool> future;
            auto client(std::make_shared<tips_req_client>(connection_l));
            client->run();
            future = client->promise.get_future();
            lock_a.unlock();
            failed = consume_future(future);
            lock_a.lock();

            if (failed)
                LOG_INFO(log) << "tips_req failed, reattempting";
            else
            {
                puller.Init(client->request, client->response);
            }
        }
        return failed;
    }

    void bootstrap_attempt::request_pull(std::unique_lock<std::mutex> &lock_a)
    {
        LOG_DEBUG(log) << "bootstrap_attempt::request_pull: start";
        while(puller.GetNumWaitingPulls() > 0)
        {
            auto connection_l(connection(lock_a));
            if (connection_l)
            {
                working_clients.push_back(connection_l);
                auto pull = puller.GetPull();
                assert(pull.get() != nullptr);
                /*
                                // The bulk_pull_client destructor attempt to requeue_pull
                                // which can cause a deadlock if this is the last reference
                                // Dispatch request in an external thread in case it needs to be destroyed
                                background([connection_l, pull]() {
                */
                auto client(std::make_shared<bulk_pull_client>(connection_l, pull));
                client->run();
            } else {
                break;
            }
        }
    }

    void bootstrap_attempt::run()
    {
        LOG_DEBUG(log) << "bootstrap_attempt::run begin {" << std::endl;
        populate_connections();
        std::unique_lock <std::mutex> lock(mutex);
        auto tips_failure(true);
        uint32_t retry = 0;
        while (!stopped && tips_failure)
        {
            retry++;
            if (retry > bootstrap_max_retry)
            {
                LOG_ERROR(log) << "bootstrap_attempt::run error too many retries for request_tips";
                return; // Couldn't get tips in this attempt
            }
            tips_failure = request_tips(lock);
        }

        while (!stopped)
        {
            if (!puller.AllDone())
            {
                request_pull(lock);
                LOG_DEBUG(log) << "bootstrap_attempt::run, wait...";
                condition.wait(lock);
            }
            else
            {
                break;
            }
        }

        if (!stopped)
            LOG_INFO(log) << "Completed pulls";
        else
            clean(); //TODO Must wait till threads using this have stopped, else mem fault...

        LOG_DEBUG(log) << "bootstrap_attempt::run end }" << std::endl;
    }

    std::shared_ptr<bootstrap_client> bootstrap_attempt::connection(std::unique_lock<std::mutex> &lock_a)
    {
        while (!stopped && idle_clients.empty()) {
            // The code has been observed to wait here if idle is empty...
            // First we create connections in populate_connection, then those clients call run and
            // if they successfully connect, the new connection goes into the idle queue. If we have
            // no connections, we need to wait for them.
            LOG_DEBUG(log) << "bootstrap_attempt::connection:: wait... "
                           << "!idle.empty(): " << !idle_clients.empty()
                           << " stopped: " << stopped;
            //condition.wait(lock_a);
        }
        std::shared_ptr <bootstrap_client> result;
        if (!idle_clients.empty()) {
            result = idle_clients.back();
            idle_clients.pop_back();
        } else {
            LOG_DEBUG(log) << "idle is empty: stopped: " << stopped
                           << " !idle.empty(): " << !(idle_clients.empty());
        }
        return result;
    }

    bool bootstrap_attempt::consume_future(std::future<bool> &future_a) {
        bool result;
        try {
            std::chrono::system_clock::time_point minute_passed
                    = std::chrono::system_clock::now() + std::chrono::seconds(60);
            if (std::future_status::ready == future_a.wait_until(minute_passed)) {
                result = future_a.get();
            } else {
                // future timed out, return error.
                LOG_DEBUG(log) << "bootstrap_attempt::consume_future: timeout" << std::endl;
                result = true;
            }
        }
        catch (std::future_error &) {
            result = true;
        }
        return result;
    }

    size_t bootstrap_attempt::target_connections(size_t need)
    {
        need = std::max(need, puller.GetNumWaitingPulls());
        assert(max_connected >= connections);
        size_t allowed = max_connected - connections;
        return std::min(allowed, need);
    }

    void bootstrap_attempt::populate_connections(size_t need)
    {
        LOG_DEBUG(log) << "bootstrap_attempt::populate_connections begin {";
        std::unique_lock <std::mutex> lock(mutex);
        auto delta = target_connections(need);

        LOG_DEBUG(log) << "bootstrap_attempt:: delta: " << delta
                       << " connections: " << connections
                       << " working_clients.size: " << working_clients.size()
                       << " idle_clients.size: " << idle_clients.size();

        if(delta == 0)
        {
            LOG_DEBUG(log) << __func__ << " don't need more connections";
            return;
        }

        std::vector<logos::endpoint> peers;
        session_id = peer_provider.get_peers(session_id, peers, delta);
        if(peers.size() == 0)
        {
            LOG_INFO(log) << __func__ << " cannot get peers";
            return;
        }

        for (auto peer : peers)
        {
            auto address = peer.address();
            /*
            //TODO for testing: filter out local address
                        if (address.to_v6().to_string() == std::string("::ffff:172.1.1.100")) {
                            continue; // RGD Hack TODO remove this...
                        }

                        if (peer != logos::endpoint(boost::asio::ip::address_v6::any(), 0))
            */

            auto client(std::make_shared<bootstrap_client>(alarm, *this,//shared_from_this(),
                                                           logos::tcp_endpoint(address, BOOTSTRAP_PORT)));
            //client->run();
            /*
             * TODO when to give up the attempt
                        if (connections == 0)
                        {
                            LOG_INFO(log) << "Bootstrap stopped because there are no peers";
                            stopped = true;
                            condition.notify_all();
                        }
            */
        }

        if (!stopped) {
            std::weak_ptr <bootstrap_attempt> this_w(shared_from_this());
            alarm.add(std::chrono::steady_clock::now() + std::chrono::seconds(1), [this_w]()
            {
                if (auto this_l = this_w.lock()) {
                    this_l->populate_connections();
                }
            });
        }
        LOG_DEBUG(log) << "bootstrap_attempt::populate_connections end }";
    }

    void bootstrap_attempt::add_connection(logos::endpoint const &endpoint_a)
    {
        auto client(std::make_shared<bootstrap_client>(alarm, shared_from_this(),
                                                       logos::tcp_endpoint(endpoint_a.address(),
                                                                           endpoint_a.port())));
        //client->run();
    }

    void bootstrap_attempt::pool_connection(std::shared_ptr<bootstrap_client> client_a)
    {
        LOG_DEBUG(log) << __func__;
        std::lock_guard <std::mutex> lock(mutex);
        idle_clients.push_front(client_a);
        condition.notify_all();
    }

    void bootstrap_attempt::stop()
    {
        LOG_DEBUG(log) << "bootstrap_attempt::stop" << std::endl;
        std::lock_guard <std::mutex> lock(mutex);
        clean();
    }

    void bootstrap_attempt::clean()
    {
        LOG_TRACE(log) << __func__;
        stopped = true;
        condition.notify_all();
        for (auto client : idle_clients)
        {
            LOG_DEBUG(log) << "bootstrap_attempt::stop: socket->close " << client->socket.remote_endpoint();
            client->socket.close();
        }
        idle_clients.clear();
        for (auto client : working_clients)
        {
            LOG_DEBUG(log) << "bootstrap_attempt::stop: socket->close " << client->socket.remote_endpoint();
            client->socket.close();
        }
        working_clients.clear();
        /*
        //TODO Stop on-going tip request and set promise to unblock the thread in attempt::run
         if (auto i = tips.lock()) {
                    try {
                        i->promise.set_value(true);
                    }
                    catch (std::future_error &e) {
                        LOG_DEBUG(log) << "bootstrap_attempt::stop caught error in setting promise: " << e.what();
                    }
                }
        */
    }
}
