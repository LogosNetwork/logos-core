#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>
#include <logos/bootstrap/tips.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/p2p.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>
#include <logos/lib/trace.hpp>


namespace Bootstrap
{
    bootstrap_initiator::bootstrap_initiator(Service & service,
            logos::alarm & alarm,
            uint8_t max_connected)
    : service(service)
    , alarm(alarm)
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
        std::unique_lock<std::mutex> lock(mutex);
        if (!stopped && attempt == nullptr) {
            attempt = std::make_shared<bootstrap_attempt>(alarm);
            condition.notify_all();
        }
    }

    void bootstrap_initiator::bootstrap(logos::endpoint const &endpoint_a)
    {
        //cannot add endpoint_a to peer list, since it could be
        //one of the delegate
        std::unique_lock<std::mutex> lock(mutex);
        if (!stopped) {
            while (attempt != nullptr) {
                attempt->stop();
                condition.wait(lock);
            }
            attempt = std::make_shared<bootstrap_attempt>(alarm);
            attempt->add_connection(endpoint_a);
            condition.notify_all();
        }
    }

    void bootstrap_initiator::run_bootstrap()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopped) {
            if (attempt != nullptr) {
                lock.unlock();
                attempt->run(); // NOTE Call bootstrap_attempt::run
                lock.lock();
                attempt = nullptr; // stop is called in destructor...
                condition.notify_all();
            } else {
                condition.wait(lock);
            }
        }
    }

    bool bootstrap_initiator::in_progress()
    {
        return current_attempt() != nullptr;
    }

    std::shared_ptr<bootstrap_attempt> bootstrap_initiator::current_attempt()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return attempt;
    }

    void bootstrap_initiator::stop()
    {
        std::unique_lock<std::mutex> lock(mutex);
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

    bootstrap_listener::bootstrap_listener(Service &service_a,
            Store & store,
            std::string & local_address,
            uint16_t port_a,
            uint8_t max_accepted)
    : acceptor(service_a)
    //, local(boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v6::any(), port_a))
    , local(get_endpoint(local_address))
    , service(service_a)
    , store(store)
    , max_accepted(max_accepted)
    {
        LOG_DEBUG(log) << __func__ << " port=" << port_a;
    }

    void bootstrap_listener::start()
    {
        //acceptor.open (local.protocol ());
        acceptor.open(endpoint().protocol());
        acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

        boost::system::error_code ec;
        //acceptor.bind (local, ec); // RGD
        acceptor.bind(endpoint(), ec);
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
        decltype(connections) connections_l;
        {
            std::lock_guard<std::mutex> lock(mutex);
            on = false;
            connections_l.swap(connections);
        }
        LOG_DEBUG(log) << "bootstrap_listener::stop: acceptor->close";
        acceptor.close();
        for (auto &i : connections_l)
        {
            auto connection(i.second.lock());
            if (connection)
            {
                LOG_DEBUG(log) << "bootstrap_listener::stop: socket->close";
                connection->socket->close();
            }
        }
    }

    void bootstrap_listener::accept_connection()
    {
        auto socket(std::make_shared<boost::asio::ip::tcp::socket>(service));
        acceptor.async_accept(*socket, [this, socket](boost::system::error_code const &ec)
        {
            accept_action(ec, socket);
        });
    }

    void bootstrap_listener::accept_action(boost::system::error_code const &ec,
                                           std::shared_ptr<boost::asio::ip::tcp::socket> socket_a)
    {
        if (!ec)
        {
            accept_connection();
            auto connection(std::make_shared<bootstrap_server>(socket_a));//TODO
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (connections.size() < max_accepted && acceptor.is_open())
                {
                    LOG_DEBUG(log) << "bootstrap_listener::accept_action: " << connections.size()
                                        << " acceptor.is_open(): " << acceptor.is_open();
                    connections[connection.get()] = connection;//TODO
                    connection->receive();
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
}