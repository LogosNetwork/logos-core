#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/tips.hpp>
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
    bootstrap_initiator::bootstrap_initiator(logos::alarm & alarm,
       		Store & store,
			BlockCache & cache,
			PeerInfoProvider & peer_provider,
            uint8_t max_connected)
    : service(alarm.service)
    , alarm(alarm)
    , store(store)
    , cache(cache)
    , peer_provider(peer_provider)
    , stopped(false)
    , max_connected(max_connected)
    , thread([this]() { run_bootstrap(); })
    {}

    bootstrap_initiator::~bootstrap_initiator()
    {
        stop();
        thread.join();
    }

    void bootstrap_initiator::bootstrap()
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!stopped && attempt == nullptr) {
            attempt = std::make_shared<bootstrap_attempt>(alarm,
            		store,
					cache,
					peer_provider,
					max_connected);
            condition.notify_all();
        }
    }

    void bootstrap_initiator::bootstrap(logos::endpoint const &endpoint_a)
    {
        //cannot add endpoint_a to peer list, since it could be
        //one of the delegate
        std::unique_lock<std::mutex> lock(mtx);
        if (!stopped)
        {
            if(attempt != nullptr)
            {
                attempt->add_connection(endpoint_a);
            }
            else
            {
				attempt = std::make_shared<bootstrap_attempt>(alarm,
						store,
						cache,
						peer_provider,
						max_connected);
				attempt->add_connection(endpoint_a);
				condition.notify_all();
            }
        }
    }

    void bootstrap_initiator::run_bootstrap()
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (!stopped)
        {
            if (attempt != nullptr) {
                lock.unlock();
                attempt->run();
                lock.lock();
                attempt = nullptr; // stop is called in destructor...
                condition.notify_all();
            } else {
                condition.wait(lock);
            }
        }
    }

    bool bootstrap_initiator::check_progress()
    {
    	std::lock_guard<std::mutex> lock(mtx);
    	if(attempt == nullptr)
    	{
    		return false;
    	}

#ifdef BOOTSTRAP_PROGRESS
    	if(get_block_progress_and_reset() == 0)//TODO ==0
    	{
    		attempt = nullptr;
    		return false;
    	}
#endif
    	return true;
    }

    void bootstrap_initiator::stop()
    {
        std::unique_lock<std::mutex> lock(mtx);
        stopped = true;
        if (attempt != nullptr) {
            attempt->stop();
        }
        condition.notify_all();
    }

    //////////////////////////////////////////////////////////////////////////////////

    boost::asio::ip::tcp::endpoint get_endpoint(std::string & address)
    {
        return boost::asio::ip::tcp::endpoint
                (boost::asio::ip::address_v6::from_string(std::string("::ffff:") + address),
                        BOOTSTRAP_PORT);
    }

    bootstrap_listener::bootstrap_listener(logos::alarm & alarm,
            Store & store,
            std::string & local_address,
            //uint16_t port_a,
            uint8_t max_accepted)
    : acceptor(alarm.service)
    , alarm(alarm)
    //, local(boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v6::any(), port_a))
    , local(get_endpoint(local_address))
    , service(alarm.service)
    , store(store)
    , max_accepted(max_accepted)
    {
        LOG_DEBUG(log) << __func__;// << " port=" << port_a;
    }

    bootstrap_listener::~bootstrap_listener()
    {
        stop();
    }

    void bootstrap_listener::start()
    {
    	acceptor.open (local.protocol ());
        //acceptor.open(endpoint().protocol());
        acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

        boost::system::error_code ec;
        acceptor.bind (local, ec); // RGD
        //acceptor.bind(endpoint(), ec);
        if (ec)
        {
            LOG_FATAL(log) << "Error while binding for bootstrap on port " << local.port()
                           << " " << ec.message();
            trace_and_halt();
        }

        acceptor.listen();
        accept_connection();
    }

    void bootstrap_listener::stop()
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

    void bootstrap_listener::accept_connection()
    {
        auto socket(std::make_shared<BoostSocket>(service));
        acceptor.async_accept(*socket, [this, socket](boost::system::error_code const &ec)
        {
            accept_action(ec, socket);
        });
    }

    void bootstrap_listener::accept_action(boost::system::error_code const &ec,
                                           std::shared_ptr<BoostSocket> socket_a)
    {
        if (!ec)
        {
            accept_connection();
            auto connection(std::make_shared<bootstrap_server>(*this, *socket_a, store));
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

    void bootstrap_listener::remove_connection(std::shared_ptr<bootstrap_server> server)
    {
    	LOG_DEBUG(log) << __func__;
    	std::lock_guard <std::mutex> lock(mtx);
    	connections.erase(server);
    	condition.notify_all();
    }
}
