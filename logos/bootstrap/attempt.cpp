#include <logos/bootstrap/attempt.hpp>
#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/pull_connection.hpp>
#include <logos/bootstrap/tip_connection.hpp>

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
    , max_connected(max_connected)
    , session_id(PeerInfoProvider::GET_PEER_NEW_SESSION)
    , puller(cache)
    , stopped(false)
    {
        LOG_DEBUG(log) << "Starting bootstrap_attempt";
    }

    bootstrap_attempt::~bootstrap_attempt()
    {
        LOG_DEBUG(log) << "Exiting bootstrap_attempt";
        stop();
        if(session_id != PeerInfoProvider::GET_PEER_NEW_SESSION)
            peer_provider.close_session(session_id);
    }

    void bootstrap_attempt::run()
    {
        LOG_DEBUG(log) << "bootstrap_attempt::run begin {";

        {
        	auto request(TipSet::CreateTipSet(store));
            LOG_TRACE(log) <<"bootstrap_attempt::run my_tips \n" << request;
        }

        //std::unique_lock<std::mutex> lock(mtx);
        auto tips_failure(true);
        while (!stopped && tips_failure)
        {
            tips_failure = request_tips();
        }
        LOG_TRACE(log) <<"bootstrap_attempt::run 2";
        while (!stopped)
        {
            LOG_TRACE(log) <<"bootstrap_attempt::run 3";
            if (!puller.AllDone())
            {
                LOG_TRACE(log) <<"bootstrap_attempt::run 4";

                request_pull();
                LOG_TRACE(log) << "bootstrap_attempt::run, wait...";
                std::unique_lock<std::mutex> lock(mtx);
                condition.wait(lock);
                LOG_TRACE(log) << "bootstrap_attempt::run, wakeup";
            }
            else
            {
            	//std::cout<<"bootstrap_attempt::run puller.AllDone()"<<std::endl;
                break;
            }
        }

        if (!stopped)
            LOG_DEBUG(log) << "bootstrap_attempt::run end. Completed pulls}";
        else
            LOG_DEBUG(log) << "bootstrap_attempt::run end. Stopped pulls}";
    }

    bool bootstrap_attempt::request_tips()
    {
    	LOG_TRACE(log) << "bootstrap_attempt::"<<__func__;
        // NOTE: Get the connection from the pool and get tips.
        auto failed(true);
        auto connection_l(get_connection());
        if (connection_l != nullptr)
        {
            auto client = std::make_shared<tips_req_client>(connection_l, store);
            client->run();
            std::future<bool> future = client->promise.get_future();
            failed = consume_future(future);

            if (failed)
                LOG_INFO(log) << "tips_req failed, reattempting";
            else
            {
#ifdef BOOTSTRAP_PROGRESS
            	block_progressed();
#endif
                puller.Init(client->request, client->response);
            }
        }
        return failed;
    }

    bool bootstrap_attempt::consume_future(std::future<bool> &future_a)
    {
    	LOG_TRACE(log) << "bootstrap_attempt::"<<__func__;
        bool result;
        try {
            std::chrono::system_clock::time_point minute_passed
                    = std::chrono::system_clock::now() + std::chrono::seconds(60);
            if (std::future_status::ready == future_a.wait_until(minute_passed))
            {
                result = future_a.get();
            } else {
                // future timed out, return error.
                LOG_DEBUG(log) << "bootstrap_attempt::consume_future: timeout";
                result = true;
            }
        }
        catch (std::future_error &) {
            result = true;
        }
        return result;
    }

    void bootstrap_attempt::request_pull()
    {
        LOG_DEBUG(log) << "bootstrap_attempt::request_pull: start";
        while(puller.GetNumWaitingPulls() > 0)
        {
            auto connection_l = get_connection();
            if (connection_l)
            {
                auto client(std::make_shared<bulk_pull_client>(connection_l, puller));
                client->run();
            }
            else
            {
                break;
            }
        }
    }

    std::shared_ptr<bootstrap_client> bootstrap_attempt::get_connection()
    {
    	LOG_TRACE(log) << "bootstrap_attempt::"<<__func__;

    	std::unique_lock<std::mutex> lock(mtx);
        while (!stopped && idle_clients.empty())
        {
        	if(populate_connections(1))
        	{
				LOG_DEBUG(log) << "bootstrap_attempt::get_connection: "
							   << "trying to async-connect, wait... "
							   << "idle.empty()=" << idle_clients.empty()
							   << " stopped=" << stopped;
				condition.wait(lock);
				LOG_DEBUG(log) << "bootstrap_attempt::get_connection: wakeup ";
        	}
        	else
        	{
        		LOG_DEBUG(log) << "bootstrap_attempt::get_connection: will retry after sleep";
        		lock.unlock();
        		usleep(1000000);//TODO
        		lock.lock();
        	}
        }
        std::shared_ptr <bootstrap_client> result;
        if (!stopped && !idle_clients.empty())
        {
        	auto itor = idle_clients.begin();
        	result = *itor;
            idle_clients.erase(itor);
            working_clients.insert(result);
            LOG_DEBUG(log) << "bootstrap_attempt::" <<__func__
            				<<" got a connection";
        }
        else
        {
            LOG_DEBUG(log) << "bootstrap_attempt::" <<__func__
            				<< " idle_empty=" << idle_clients.empty()
							<< " stopped=" << stopped;
        }
        return result;
    }

    size_t bootstrap_attempt::target_connections(size_t need)
    {
        need = std::max(need, puller.GetNumWaitingPulls());
        //std::lock_guard <std::mutex> lock(mtx);
        auto num_con = working_clients.size() +
        		idle_clients.size() +
				connecting_clients.size();
        assert(max_connected >= num_con);
        size_t allowed = max_connected - num_con;
        return std::min(allowed, need);
    }

    bool bootstrap_attempt::populate_connections(size_t need)
    {
        LOG_DEBUG(log) << "bootstrap_attempt::populate_connections begin {";

        uint8_t delta = target_connections(need);

        LOG_DEBUG(log) << "bootstrap_attempt:: delta: " << (uint)delta
                       << " working_clients.size: " << working_clients.size()
                       << " idle_clients.size: " << idle_clients.size();

        if(delta == 0)
        {
            LOG_DEBUG(log) << "bootstrap_attempt::"<<__func__
            				<< " don't need more connections";
            return false;
        }

        std::vector<logos::endpoint> peers;
        session_id = peer_provider.get_peers(session_id, peers, delta);
        if(peers.size() == 0)
        {
            LOG_INFO(log) << "bootstrap_attempt::"<<__func__
            				<< " cannot get peers";
            return false;
        }

        for (auto peer : peers)
        {
        	LOG_DEBUG(log) << "bootstrap_attempt::populate_connection, "
        					<<"peer " << peer.address().to_string();
        	add_connection(peer);
        }

        LOG_DEBUG(log) << "bootstrap_attempt::populate_connections end }";
        return true;
    }

	void bootstrap_attempt::add_connection(logos::endpoint const &endpoint_a)
	{
    	LOG_TRACE(log) << "bootstrap_attempt::"<<__func__
    			<< ", peer ip: " << endpoint_a.address().to_string();
		logos::tcp_endpoint e (endpoint_a.address(), BOOTSTRAP_PORT);
		auto client = std::make_shared<bootstrap_client>(*this, e);
		connecting_clients.insert(client);
		client->Connect([client, this](bool connected)
        {
        	LOG_DEBUG(log) << "bootstrap_client::bootstrap_client: connected="
        				<< connected;
        	std::lock_guard<std::mutex> lock(mtx);
        	connecting_clients.erase(client);
        	if(stopped)
        	{
        		condition.notify_all();//TODO check if need lock
        	}
        	else if(connected)
        	{
        		pool_connection (client, true);
        	}
        	else
        	{
        		condition.notify_all();//TODO check if need lock
        	}
        });
	}

    void bootstrap_attempt::remove_connection(std::shared_ptr<bootstrap_client> client, bool blacklist)
    {
    	LOG_TRACE(log) << "bootstrap_attempt::"<<__func__;
    	if(blacklist)
    	{
    		logos::endpoint e(client->PeerAddress(), BOOTSTRAP_PORT);
    		peer_provider.add_to_blacklist(e);
    	}
    	std::lock_guard <std::mutex> lock(mtx);
    	working_clients.erase(client);
    	idle_clients.erase(client);
    	condition.notify_all();
    }

    void bootstrap_attempt::pool_connection(std::shared_ptr<bootstrap_client> client, bool with_lock)
    {
    	LOG_TRACE(log) << "bootstrap_attempt::"<<__func__;
    	if(!with_lock)
    		std::lock_guard <std::mutex> lock(mtx);
        idle_clients.insert(client);
    	working_clients.erase(client);
        condition.notify_all();
    }

    void bootstrap_attempt::stop()
    {
        LOG_DEBUG(log) << "bootstrap_attempt::stop";
        stopped = true;

        std::unique_lock<std::mutex> lock(mtx);

        for (auto client : idle_clients)
        {
            LOG_DEBUG(log) << "bootstrap_attempt::stop: idle_clients socket->close "
            			   << client->PeerAddress();
            client->Disconnect();
        }
        idle_clients.clear();

        for (auto client : connecting_clients)
		{
			LOG_DEBUG(log) << "bootstrap_attempt::stop: connecting_clients socket->close "
						   << client->PeerAddress();
			client->Disconnect();
		}
        for (auto client : working_clients)
        {
            LOG_DEBUG(log) << "bootstrap_attempt::stop: working_clients socket->close "
            		       << client->PeerAddress();
            client->Disconnect();
        }

        /*
         * don't call connecting_clients and working_clients.clear();
         * their clients are used by other threads
         * test if they are empty and wait if not
         */
        while (!connecting_clients.empty() || !working_clients.empty())
        {
            LOG_TRACE(log) << "bootstrap_attempt::stop: before wait"
            		<< " connecting_clients.empty=" << connecting_clients.empty()
					<< " working_clients.empty ="<<working_clients.empty();
        	condition.wait(lock);
            LOG_TRACE(log) << "bootstrap_attempt::stop: after wait";
        }

        condition.notify_all();//needed
    }
}

///*
////TODO Stop on-going tip request and set promise to unblock the thread in attempt::run
// if (auto i = tips.lock()) {
//            try {
//                i->promise.set_value(true);
//            }
//            catch (std::future_error &e) {
//                LOG_DEBUG(log) << "bootstrap_attempt::stop caught error in setting promise: " << e.what();
//            }
//        }
//*/
//        if (!stopped) {
//            std::weak_ptr <bootstrap_attempt> this_w(shared_from_this());
//            alarm.add(std::chrono::steady_clock::now() + std::chrono::seconds(1), [this_w]()
//            {
//                if (auto this_l = this_w.lock()) {
//                    this_l->populate_connections();
//                }
//            });
//        }
//				//                auto pull = puller.GetPull();
//				//                assert(pull.get() != nullptr);
//                /*
//                                // The bulk_pull_client destructor attempt to requeue_pull
//                                // which can cause a deadlock if this is the last reference
//                                // Dispatch request in an external thread in case it needs to be destroyed
//                                background([connection_l, pull]() {
//                */
//    auto address = peer.address();
//            /*
//            //TODO for testing: filter out local address
//                        if (address.to_v6().to_string() == std::string("::ffff:172.1.1.100")) {
//                            continue; // RGD Hack TODO remove this...
//                        }
//
//                        if (peer != logos::endpoint(boost::asio::ip::address_v6::any(), 0))
//            */
//
//            auto client(std::make_shared<bootstrap_client>(shared_from_this(),
//                                                           logos::tcp_endpoint(address, BOOTSTRAP_PORT)));
//            //client->run();
//            /*
//             * TODO when to give up the attempt
//                        if (connections == 0)
//                        {
//                            LOG_INFO(log) << "Bootstrap stopped because there are no peers";
//                            stopped = true;
//                            condition.notify_all();
//                        }
//            */
