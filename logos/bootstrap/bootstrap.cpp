#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>
#include <logos/bootstrap/bulk_push.hpp>
#include <logos/bootstrap/frontier.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>

#define _DEBUG 1

static std::vector<logos::request_info> req_s;
static std::shared_ptr<logos::bootstrap_client> default_client;

logos::socket_timeout::socket_timeout (logos::bootstrap_client & client_a) :
ticket (0),
client (client_a)
{
}

void logos::socket_timeout::start (std::chrono::steady_clock::time_point timeout_a)
{
	auto ticket_l (++ticket);
	std::weak_ptr<logos::bootstrap_client> client_w (client.shared ());
	client.node->alarm.add (timeout_a, [client_w, ticket_l]() { // RGD: If we timeout, we disconnect.
		if (auto client_l = client_w.lock ())
		{
			if (client_l->timeout.ticket == ticket_l)
			{
#ifdef _DEBUG
                std::cout << "logos::socket_timeout::start: socket->close" << std::endl;
#endif
				client_l->socket.close ();
				if (client_l->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (client_l->node->log) << boost::str (boost::format ("Disconnecting from %1% due to timeout") % client_l->socket.remote_endpoint ());
				}
			}
		}
	});
}

void logos::socket_timeout::stop ()
{
#ifdef _DEBUG
    std::cout << "logos::socket_timeout::stop:" << std::endl;
#endif
	++ticket;
}

logos::bootstrap_client::bootstrap_client (std::shared_ptr<logos::node> node_a, std::shared_ptr<logos::bootstrap_attempt> attempt_a, logos::tcp_endpoint const & endpoint_a) :
node (node_a),
attempt (attempt_a),
socket (node_a->service),
timeout (*this),
endpoint (endpoint_a),
start_time (std::chrono::steady_clock::now ()),
block_count (0),
pending_stop (false),
hard_stop (false)
{
    if(attempt)
	    ++attempt->connections; // RGD: Number of connection attempts.
}

logos::bootstrap_client::~bootstrap_client ()
{
    if(attempt)
	    --attempt->connections;
}

double logos::bootstrap_client::block_rate () const
{
	auto elapsed = elapsed_seconds ();
	return elapsed > 0.0 ? (double)block_count.load () / elapsed : 0.0; // RGD: Rate at which we recieve blocks.
}

double logos::bootstrap_client::elapsed_seconds () const
{
	return std::chrono::duration_cast<std::chrono::duration<double>> (std::chrono::steady_clock::now () - start_time).count ();
}

void logos::bootstrap_client::stop (bool force)
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_client::stop:" << std::endl;
#endif
	pending_stop = true;
	if (force)
	{
		hard_stop = true;
	}
}

void logos::bootstrap_client::start_timeout ()
{
	timeout.start (std::chrono::steady_clock::now () + std::chrono::seconds (20)); // RGD: Set a timeout on the connection.
}

void logos::bootstrap_client::stop_timeout ()
{
	timeout.stop ();
}

void logos::bootstrap_client::run ()
{
	auto this_l (shared_from_this ());
	start_timeout ();
	socket.async_connect (endpoint, [this_l](boost::system::error_code const & ec) { // RGD: endpoint is passed into the constructor of bootstrap_client, attempt to connect.
		this_l->stop_timeout ();
		if (!ec)
		{
			if (this_l->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Connection established to %1%") % this_l->endpoint);
			}
            default_client = this_l->shared_from_this(); // FIXME
#ifdef _DEBUG
            std::cout << "logos::bootstrap_client::run: pool_connection called" << std::endl;
#endif
			this_l->attempt->pool_connection (this_l->shared_from_this ()); // RGD: Add connection, updates idle queue.
		}
		else
		{
			if (this_l->node->config.logging.network_logging ())
			{
				switch (ec.value ())
				{
					default:
						BOOST_LOG (this_l->node->log) << boost::str (boost::format ("Error initiating bootstrap connection to %1%: %2%") % this_l->endpoint % ec.message ());
						break;
					case boost::system::errc::connection_refused:
					case boost::system::errc::operation_canceled:
					case boost::system::errc::timed_out:
					case 995: //Windows The I/O operation has been aborted because of either a thread exit or an application request
					case 10061: //Windows No connection could be made because the target machine actively refused it
						break;
				}
			}
		}
	});
}

std::shared_ptr<logos::bootstrap_client> logos::bootstrap_client::shared ()
{
	return shared_from_this ();
}

logos::bootstrap_attempt::bootstrap_attempt (std::shared_ptr<logos::node> node_a) :
next_log (std::chrono::steady_clock::now ()),
connections (0),
pulling (0),
node (node_a),
account_count (0),
total_blocks (0),
stopped (false)
{
	BOOST_LOG (node->log) << "Starting bootstrap attempt";
	node->bootstrap_initiator.notify_listeners (true);
}

logos::bootstrap_attempt::~bootstrap_attempt ()
{
	BOOST_LOG (node->log) << "Exiting bootstrap attempt";
	node->bootstrap_initiator.notify_listeners (false);
}

bool logos::bootstrap_attempt::should_log ()
{
	std::lock_guard<std::mutex> lock (mutex);
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (next_log < now)
	{
		result = true;
		next_log = now + std::chrono::seconds (15);
	}
	return result;
}

bool logos::bootstrap_attempt::request_frontier (std::unique_lock<std::mutex> & lock_a) // RGD: Get the connection from the pool (see 'connection') and get frontier.
{
	auto result (true);
	auto connection_l (connection (lock_a));
	connection_frontier_request = connection_l;
	if (connection_l)
	{
		std::future<bool> future;
		{
			auto client (std::make_shared<logos::frontier_req_client> (connection_l));
			client->run (); // RGD: Call frontier_req_client::run.
			frontiers = client;
			future = client->promise.get_future ();
		}
		lock_a.unlock ();
		result = consume_future (future);
#ifdef _DEBUG
        std::cout << "logos::bootstrap_attempt::request_frontier: future: " << result << std::endl;
#endif
		lock_a.lock ();
		if (result)
		{
#ifdef _DEBUG
            std::cout << "logos::bootstrap_attempt::request_frontier: clearing pulls" << std::endl;
#endif
			pulls.clear ();
		}
		if (node->config.logging.network_logging ())
		{
			if (!result)
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Completed frontier request, %1% out of sync accounts according to %2%") % pulls.size () % connection_l->endpoint);
			}
			else
			{
				BOOST_LOG (node->log) << "frontier_req failed, reattempting";
			}
		}
	}
	return result;
}

void logos::bootstrap_attempt::request_pull (std::unique_lock<std::mutex> & lock_a)
{ // RGD: Called form 'logos::bootstrap_attempt::run'
#ifdef _DEBUG
    std::cout << "logos::bootstrap_attempt::request_pull: start" << std::endl;
#endif
	auto connection_l (connection (lock_a));
	if (connection_l)
	{
		auto pull (pulls.front ());
#ifdef _DEBUG
        for(int i = 0; i < pulls.size(); ++i) {
            std::cout << "logos::bootstrap_attempt::request_pull: pull: " << pulls[i].delegate_id << std::endl;
        }
        std::cout << "pulls.pop_front()" << std::endl;
#endif
		pulls.pop_front ();
		auto size (pulls.size ());
		// The bulk_pull_client destructor attempt to requeue_pull which can cause a deadlock if this is the last reference
		// Dispatch request in an external thread in case it needs to be destroyed
		node->background ([connection_l, pull]() {
			auto client (std::make_shared<logos::bulk_pull_client> (connection_l, pull));
            if(pull.type == pull_type::account_pull) {
#ifdef _DEBUG
                std::cout << "logos::bootstrap_attempt::request_pull: client->request" << std::endl;
#endif
			    client->request (); // RGD: Call 'bulk_pull_client::request'.
            } else if(pull.type == pull_type::batch_block_pull) {
#ifdef _DEBUG
                std::cout << "RGDFIXME99 logos::bootstrap_attempt::request_pull: client->request_batch_block: delegate_id: " << pull.delegate_id << std::endl;
#endif
                client->request_batch_block(); // RGD: Call 'bulk_pull_client::request_batch_block'
            }
		});
	}
}

void logos::bootstrap_attempt::request_push (std::unique_lock<std::mutex> & lock_a)
{ // RGD: Called from 'logos::bootstrap_attempt::run'
	bool error (false);
	auto connection_shared = connection_frontier_request.lock ();
    if(!connection_shared) {
	    auto connection_l (connection (lock_a));
	    connection_frontier_request = connection_l;
        connection_shared = connection_frontier_request.lock();
        if(!connection_shared) {
            connection_shared = default_client; // FIXME -- Their code seems to lose a connection toward the end...
        }
    }
    if(connection_shared)
	{ 
        connection_shared->attempt->req = req_s; // FIXME -- Their code has empty req
#ifdef _DEBUG
        for(int i = 0; i < req_s.size(); ++i) {
           std::cout << "logos::bootstrap_attempt::request_push: dump request: delegate_id: " << req_s[i].delegate_id << std::endl;
        }
#endif
        req_s.clear();
		auto client (std::make_shared<logos::bulk_push_client> (connection_shared));
		client->start ();
		push = client;
		auto future (client->promise.get_future ());
		lock_a.unlock ();
		error = consume_future (future);
		lock_a.lock ();
	} else {
#ifdef _DEBUG
        std::cout << "logos::bootstrap_attempt::request_push: connection_shared is null" << std::endl;
#endif
    }
	if (node->config.logging.network_logging ())
	{
		BOOST_LOG (node->log) << "Exiting bulk push client";
		if (error)
		{
			BOOST_LOG (node->log) << "Bulk push client failed";
		}
	}
}

bool logos::bootstrap_attempt::still_pulling ()
{
	assert (!mutex.try_lock ());
	auto running (!stopped);
	auto more_pulls (!pulls.empty ());
	auto still_pulling (pulling > 0);
#ifdef _DEBUG
    std::cout << "logos::bootstrap_attempt::still_pulling: running: " << running << " more_pulls: " << more_pulls << " still_pulling: " << still_pulling << std::endl;
#endif
	return running && (more_pulls || still_pulling);
}

void logos::bootstrap_attempt::run ()
{
#ifdef _DEBUG
    std::cout << "bootstrap_attempt::run begin {" << std::endl;
#endif
	populate_connections ();
	std::unique_lock<std::mutex> lock (mutex);
	auto frontier_failure (true);
	while (!stopped && frontier_failure)
	{
		frontier_failure = request_frontier (lock);
	}
	// Shuffle pulls.
	for (int i = pulls.size () - 1; i > 0; i--)
	{
		auto k = logos::random_pool.GenerateWord32 (0, i);
		std::swap (pulls[i], pulls[k]);
	}
	while (still_pulling ()) // RGD This is it, figure out why this is false...
	{
		while (still_pulling ())
		{
			if (!pulls.empty ())
			{
				request_pull (lock); // RGD: Start of bulk_pull_client.
			}
			else
			{
				condition.wait (lock);
			}
		}
		// Flushing may resolve forks which can add more pulls
		BOOST_LOG (node->log) << "Flushing unchecked blocks";
		lock.unlock ();
		node->block_processor.flush ();
		lock.lock ();
		BOOST_LOG (node->log) << "Finished flushing unchecked blocks";
	}
	if (!stopped)
	{
		BOOST_LOG (node->log) << "Completed pulls";
	}
	request_push (lock);
	stopped = true;
	condition.notify_all ();
	idle.clear ();
#ifdef _DEBUG
    std::cout << "bootstrap_attempt::run end }" << std::endl;
#endif
}

std::shared_ptr<logos::bootstrap_client> logos::bootstrap_attempt::connection (std::unique_lock<std::mutex> & lock_a)
{
	while (!stopped && idle.empty ())
	{
#ifdef _DEBUG
        // RGD The code has been observed to deadlock here if idle is empty...
        std::cout << "logos::bootstrap_attempt::connection:: !idle.empty(): " << !idle.empty() << " stopped: " << stopped << std::endl;
#endif
		condition.wait (lock_a);
	}
#ifdef _DEBUG
    std::cout << "logos::bootstrap_attempt::connection:: getting result: " << std::endl;
#endif
	std::shared_ptr<logos::bootstrap_client> result;
	if (!idle.empty ())
	{
		result = idle.back ();
		idle.pop_back ();
	} else {
#ifdef _DEBUG
        std::cout << "idle is empty: stopped: " << stopped << " !idle.empty(): " << !(idle.empty()) << std::endl;
#endif
    }
	return result;
}

bool logos::bootstrap_attempt::consume_future (std::future<bool> & future_a)
{
	bool result;
	try
	{
		result = future_a.get ();
	}
	catch (std::future_error &)
	{
		result = true;
	}
	return result;
}

void logos::bootstrap_attempt::process_fork (MDB_txn * transaction_a, std::shared_ptr<logos::block> block_a)
{
	//CH during bootstrap, we should not get forks in Logos if we check the signatures and validate the blocks first. I will leave the function here however.
	/*std::lock_guard<std::mutex> lock (mutex);
	auto root (block_a->root ());
	if (!node->store.block_exists (transaction_a, block_a->hash ()) && node->store.root_exists (transaction_a, block_a->root ()))
	{
		std::shared_ptr<logos::block> ledger_block (node->ledger.forked_block (transaction_a, *block_a));
		if (ledger_block)
		{
			std::weak_ptr<logos::bootstrap_attempt> this_w (shared_from_this ());
			if (!node->active.start (std::make_pair (ledger_block, block_a), [this_w, root](std::shared_ptr<logos::block>) {
				    if (auto this_l = this_w.lock ())
				    {
					    logos::transaction transaction (this_l->node->store.environment, nullptr, false);
					    auto account (this_l->node->ledger.store.frontier_get (transaction, root));
					    if (!account.is_zero ())
					    {
						    this_l->requeue_pull (logos::pull_info (account, root, root));
					    }
					    else if (this_l->node->ledger.store.account_exists (transaction, root))
					    {
						    this_l->requeue_pull (logos::pull_info (root, logos::block_hash (0), logos::block_hash (0)));
					    }
				    }
			    }))
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Resolving fork between our block: %1% and block %2% both with root %3%") % ledger_block->hash ().to_string () % block_a->hash ().to_string () % block_a->root ().to_string ());
				//node->network.broadcast_confirm_req (ledger_block);
			}
		}
	}*/
}

struct block_rate_cmp
{
	bool operator() (const std::shared_ptr<logos::bootstrap_client> & lhs, const std::shared_ptr<logos::bootstrap_client> & rhs) const
	{
		return lhs->block_rate () > rhs->block_rate ();
	}
};

unsigned logos::bootstrap_attempt::target_connections (size_t pulls_remaining)
{
	if (node->config.bootstrap_connections >= node->config.bootstrap_connections_max)
	{
		return std::max (1U, node->config.bootstrap_connections_max);
	}

	// Only scale up to bootstrap_connections_max for large pulls.
	double step = std::min (1.0, std::max (0.0, (double)pulls_remaining / bootstrap_connection_scale_target_blocks));
	double target = (double)node->config.bootstrap_connections + (double)(node->config.bootstrap_connections_max - node->config.bootstrap_connections) * step;
	return std::max (1U, (unsigned)(target + 0.5f));
}

void logos::bootstrap_attempt::populate_connections ()
{
#ifdef _DEBUG
    std::cout << "bootstrap_attempt::populate_connections begin {" << std::endl;
#endif
	double rate_sum = 0.0;
	size_t num_pulls = 0;
	std::priority_queue<std::shared_ptr<logos::bootstrap_client>, std::vector<std::shared_ptr<logos::bootstrap_client>>, block_rate_cmp> sorted_connections;
	{
		std::unique_lock<std::mutex> lock (mutex);
		num_pulls = pulls.size ();
		for (auto & c : clients)
		{
			if (auto client = c.lock ())
			{
				double elapsed_sec = client->elapsed_seconds ();
				auto blocks_per_sec = client->block_rate ();
				rate_sum += blocks_per_sec;
				if (client->elapsed_seconds () > bootstrap_connection_warmup_time_sec && client->block_count > 0)
				{
					sorted_connections.push (client);
				}
				// Force-stop the slowest peers, since they can take the whole bootstrap hostage by dribbling out blocks on the last remaining pull.
				// This is ~1.5kilobits/sec.
				if (elapsed_sec > bootstrap_minimum_termination_time_sec && blocks_per_sec < bootstrap_minimum_blocks_per_sec)
				{
					if (node->config.logging.bulk_pull_logging ())
					{
						BOOST_LOG (node->log) << boost::str (boost::format ("Stopping slow peer %1% (elapsed sec %2%s > %3%s and %4% blocks per second < %5%)") % client->endpoint.address ().to_string () % elapsed_sec % bootstrap_minimum_termination_time_sec % blocks_per_sec % bootstrap_minimum_blocks_per_sec);
					}

					client->stop (true);
				}
			}
		}
	}

	auto target = target_connections (num_pulls);

	// We only want to drop slow peers when more than 2/3 are active. 2/3 because 1/2 is too aggressive, and 100% rarely happens.
	// Probably needs more tuning.
	if (sorted_connections.size () >= (target * 2) / 3 && target >= 4)
	{
		// 4 -> 1, 8 -> 2, 16 -> 4, arbitrary, but seems to work well.
		auto drop = (int)roundf (sqrtf ((float)target - 2.0f));

		if (node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Dropping %1% bulk pull peers, target connections %2%") % drop % target);
		}

		for (int i = 0; i < drop; i++)
		{
			auto client = sorted_connections.top ();

			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Dropping peer with block rate %1%, block count %2% (%3%) ") % client->block_rate () % client->block_count % client->endpoint.address ().to_string ());
			}

			client->stop (false);
			sorted_connections.pop ();
		}
	}

	if (node->config.logging.bulk_pull_logging ())
	{
		std::unique_lock<std::mutex> lock (mutex);
		BOOST_LOG (node->log) << boost::str (boost::format ("Bulk pull connections: %1%, rate: %2% blocks/sec, remaining account pulls: %3%, total blocks: %4%") % connections.load () % (int)rate_sum % pulls.size () % (int)total_blocks.load ());
	}

	if (connections < target)
	{
		auto delta = std::min ((target - connections) * 2, bootstrap_max_new_connections);
		// TODO - tune this better
		// Not many peers respond, need to try to make more connections than we need.
		for (int i = 0; i < delta; i++)
		{
			auto peer (node->peers.bootstrap_peer ());
			if (peer != logos::endpoint (boost::asio::ip::address_v6::any (), 0))
			{
				auto client (std::make_shared<logos::bootstrap_client> (node, shared_from_this (), logos::tcp_endpoint (peer.address (), peer.port ())));
				client->run ();
				std::lock_guard<std::mutex> lock (mutex);
				clients.push_back (client);
			}
			else if (connections == 0)
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Bootstrap stopped because there are no peers"));
				stopped = true;
				condition.notify_all ();
			}
		}
	}
	if (!stopped)
	{
		std::weak_ptr<logos::bootstrap_attempt> this_w (shared_from_this ());
		node->alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (1), [this_w]() {
			if (auto this_l = this_w.lock ())
			{
				this_l->populate_connections ();
			}
		});
	}
#ifdef _DEBUG
    std::cout << "bootstrap_attempt::populate_connections end }" << std::endl;
#endif
}

void logos::bootstrap_attempt::add_connection (logos::endpoint const & endpoint_a)
{
	auto client (std::make_shared<logos::bootstrap_client> (node, shared_from_this (), logos::tcp_endpoint (endpoint_a.address (), endpoint_a.port ())));
	client->run ();
}

void logos::bootstrap_attempt::pool_connection (std::shared_ptr<logos::bootstrap_client> client_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	idle.push_front (client_a);
#ifdef _DEBUG
    std::cout << "logos::bootstrap_attempt::pool_connection !idle.empty(): " << !idle.empty() << std::endl;
#endif
	condition.notify_all ();
}

void logos::bootstrap_attempt::stop ()
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_attempt::stop" << std::endl;
#endif
	std::lock_guard<std::mutex> lock (mutex);
	stopped = true;
	condition.notify_all ();
	for (auto i : clients)
	{
		if (auto client = i.lock ())
		{
#ifdef _DEBUG
            std::cout << "logos::bootstrap_attempt::stop: socket->close" << std::endl;
#endif
		    client->socket.close ();
		}
	}
	if (auto i = frontiers.lock ())
	{
		try
		{
			i->promise.set_value (true);
		}
		catch (std::future_error &)
		{
		}
	}
	if (auto i = push.lock ())
	{
		try
		{
			i->promise.set_value (true);
		}
		catch (std::future_error &)
		{
		}
	}
}

void logos::bootstrap_attempt::add_pull (logos::pull_info const & pull)
{
    std::unique_lock<std::mutex> lock (mutex);

	pulls.push_back (pull);
    if(idle.empty()) {
        idle.push_front(default_client); // RGD: FIXME There is a race condition regarding idle...
    }
    request_pull(lock); // RGD: FIXME Force this to start processing...
	condition.notify_all ();
}

void logos::bootstrap_attempt::requeue_pull (logos::pull_info const & pull_a)
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_attempt::requeue_pull: start" << std::endl;
#endif
	auto pull (pull_a);
#ifdef _DEBUG
    std::cout << "logos::bootstrap_attempt::requeue_pull: pull_a.delegate_id: " << pull_a.delegate_id << " pull.delegate_id: " << pull.delegate_id << std::endl;
#endif
    // RGD requeue_pull was called from bulk_pull.cpp
	if (++pull.attempts < bootstrap_frontier_retry_limit)
	{
		std::lock_guard<std::mutex> lock (mutex);
		pulls.push_front (pull);
		condition.notify_all ();
	}
	else if (pull.attempts == bootstrap_frontier_retry_limit)
	{
		pull.attempts++;
		std::lock_guard<std::mutex> lock (mutex);
		if (auto connection_shared = connection_frontier_request.lock ())
		{
			node->background ([connection_shared, pull]() {
				auto client (std::make_shared<logos::bulk_pull_client> (connection_shared, pull));
                if(pull.type == pull_type::account_pull) {
#ifdef _DEBUG
                    std::cout << "logos::bootstrap_attempt::requeue_pull: client->request" << std::endl;
#endif
				    client->request (); // RGD: Call 'bulk_pull_client::request'.
                } else if(pull.type == pull_type::batch_block_pull) {
#ifdef _DEBUG
                    std::cout << "RGDFIXME99 logos::bootstrap_attempt::requeue_pull: client->request_batch_block delegate_id: " << pull.delegate_id << std::endl;
#endif
                    client->request_batch_block(); // RGD: Call 'bulk_pull_client::requestBatchBlock'
                }
			});
			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Requesting pull account %1% from frontier peer after %2% attempts") % pull.account.to_account () % pull.attempts);
			}
		}
	}
	else
	{
		if (node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Failed to pull account %1% down to %2% after %3% attempts") % pull.account.to_account () % pull.end.to_string () % pull.attempts);
		}
	}
}

void logos::bootstrap_attempt::add_bulk_push_target (logos::block_hash const & head, logos::block_hash const & end)
{
	std::lock_guard<std::mutex> lock (mutex);
	bulk_push_targets.push_back (std::make_pair (head, end));
}

void logos::bootstrap_attempt::add_bulk_push_target(logos::request_info r)
{
	std::lock_guard<std::mutex> lock (mutex);
#ifdef _DEBUG
    std::cout << "logos::bootstrap_attempt::add_bulk_push_target: " << std::endl;
#endif
    req.push_back(r);
    req_s.push_back(r);
}

logos::bootstrap_initiator::bootstrap_initiator (logos::node & node_a) :
node (node_a),
stopped (false),
thread ([this]() { run_bootstrap (); })
{
}

logos::bootstrap_initiator::~bootstrap_initiator ()
{
	stop ();
	thread.join ();
}

void logos::bootstrap_initiator::bootstrap ()
{
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped && attempt == nullptr)
	{
		node.stats.inc (logos::stat::type::bootstrap, logos::stat::detail::initiate, logos::stat::dir::out);
		attempt = std::make_shared<logos::bootstrap_attempt> (node.shared ());
		condition.notify_all ();
	}
}

void logos::bootstrap_initiator::bootstrap (logos::endpoint const & endpoint_a, bool add_to_peers)
{
	if (add_to_peers)
	{
		node.peers.insert (endpoint_a, logos::protocol_version);
	}
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		while (attempt != nullptr)
		{
			attempt->stop ();
			condition.wait (lock);
		}
		node.stats.inc (logos::stat::type::bootstrap, logos::stat::detail::initiate, logos::stat::dir::out);
		attempt = std::make_shared<logos::bootstrap_attempt> (node.shared ());
		attempt->add_connection (endpoint_a);
		condition.notify_all ();
	}
}

void logos::bootstrap_initiator::run_bootstrap ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (attempt != nullptr)
		{
			lock.unlock ();
			attempt->run (); // RGD Call bootstrap_attempt::run
			lock.lock ();
			attempt = nullptr;
			condition.notify_all ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void logos::bootstrap_initiator::add_observer (std::function<void(bool)> const & observer_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	observers.push_back (observer_a);
}

bool logos::bootstrap_initiator::in_progress ()
{
	return current_attempt () != nullptr;
}

std::shared_ptr<logos::bootstrap_attempt> logos::bootstrap_initiator::current_attempt ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return attempt;
}

void logos::bootstrap_initiator::stop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	stopped = true;
	if (attempt != nullptr)
	{
		attempt->stop ();
	}
	condition.notify_all ();
}

void logos::bootstrap_initiator::notify_listeners (bool in_progress_a)
{
	for (auto & i : observers)
	{
		i (in_progress_a);
	}
}

void logos::bootstrap_initiator::process_fork (MDB_txn * transaction, std::shared_ptr<logos::block> block_a)
{
	std::unique_lock<std::mutex> lock (mutex);
	if (attempt != nullptr)
	{
		attempt->process_fork (transaction, block_a);
	}
}

logos::bootstrap_listener::bootstrap_listener (boost::asio::io_service & service_a, uint16_t port_a, logos::node & node_a) :
acceptor (service_a),
local (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::any (), port_a)),
service (service_a),
node (node_a)
{
}

void logos::bootstrap_listener::start ()
{
	acceptor.open (local.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

	boost::system::error_code ec;
	acceptor.bind (local, ec);
	if (ec)
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Error while binding for bootstrap on port %1%: %2%") % local.port () % ec.message ());
		throw std::runtime_error (ec.message ());
	}

	acceptor.listen ();
	accept_connection ();
}

void logos::bootstrap_listener::stop ()
{
	decltype (connections) connections_l;
	{
		std::lock_guard<std::mutex> lock (mutex);
		on = false;
		connections_l.swap (connections);
	}
#ifdef _DEBUG
   std::cout << "logos::bootstrap_listener::stop: acceptor->close" << std::endl;
#endif
	acceptor.close ();
	for (auto & i : connections_l)
	{
		auto connection (i.second.lock ());
		if (connection)
		{
#ifdef _DEBUG
            std::cout << "logos::bootstrap_listener::stop: socket->close" << std::endl;
#endif
			connection->socket->close ();
		}
	}
}

void logos::bootstrap_listener::accept_connection ()
{
	auto socket (std::make_shared<boost::asio::ip::tcp::socket> (service));
	acceptor.async_accept (*socket, [this, socket](boost::system::error_code const & ec) {
		accept_action (ec, socket);
	});
}

void logos::bootstrap_listener::accept_action (boost::system::error_code const & ec, std::shared_ptr<boost::asio::ip::tcp::socket> socket_a)
{
	if (!ec)
	{
		accept_connection ();
		auto connection (std::make_shared<logos::bootstrap_server> (socket_a, node.shared ()));
		{
			std::lock_guard<std::mutex> lock (mutex);
			if (connections.size () < node.config.bootstrap_connections_max && acceptor.is_open ())
			{
				connections[connection.get ()] = connection;
				connection->receive ();
			}
		}
	}
	else
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Error while accepting bootstrap connections: %1%") % ec.message ());
	}
}

boost::asio::ip::tcp::endpoint logos::bootstrap_listener::endpoint ()
{
	return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), local.port ());
}

logos::bootstrap_server::~bootstrap_server ()
{
	if (node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (node->log) << "Exiting bootstrap server";
	}
	std::lock_guard<std::mutex> lock (node->bootstrap.mutex);
	node->bootstrap.connections.erase (this);
}

logos::bootstrap_server::bootstrap_server (std::shared_ptr<boost::asio::ip::tcp::socket> socket_a, std::shared_ptr<logos::node> node_a) :
socket (socket_a),
node (node_a)
{
    std::cout << "logos::bootstrap_server::bootstrap_server: " << node << std::endl;
}

void logos::bootstrap_server::receive ()
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::receive" << std::endl;
#endif
	auto this_l (shared_from_this ());
	boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), 8), [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->receive_header_action (ec, size_a);
	});
}

void logos::bootstrap_server::receive_header_action (boost::system::error_code const & ec, size_t size_a)
{ // RGD: Start of server request handling.
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::receive_header_action" << std::endl;
#endif
	if (!ec)
	{
		assert (size_a == 8);
		logos::bufferstream type_stream (receive_buffer.data (), size_a);
		uint8_t version_max;
		uint8_t version_using;
		uint8_t version_min;
		logos::message_type type;
		std::bitset<16> extensions;
		if (!logos::message::read_header (type_stream, version_max, version_using, version_min, type, extensions))
		{
			switch (type)
			{ // RGDTODO
              // Potentially add handler for batch_frontier see frontier_req and receive_frontier_req_action
				case logos::message_type::batch_blocks_pull:
				{
                    std::cout << "RGDFIXME17 line: " << __LINE__ << " file: " << __FILE__ << std::endl;
					node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::bulk_pull, logos::stat::dir::in);
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (
                        receive_buffer.data () + 8, 
                        logos::bulk_pull::SIZE
                    ), [this_l](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_action (ec, size_a);
					});
					break;
				}
				case logos::message_type::bulk_pull:
				{
                    std::cout << "RGDFIXME17 line: " << __LINE__ << " file: " << __FILE__ << std::endl;
					node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::bulk_pull, logos::stat::dir::in);
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 8, sizeof (logos::uint256_union) + sizeof (logos::uint256_union)), [this_l](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_action (ec, size_a);
					});
					break;
				}
				case logos::message_type::bulk_pull_blocks:
				{
                    std::cout << "RGDFIXME17 line: " << __LINE__ << " file: " << __FILE__ << std::endl;
					node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::bulk_pull_blocks, logos::stat::dir::in);
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + logos::bootstrap_message_header_size, sizeof (logos::uint256_union) + sizeof (logos::uint256_union) + sizeof (bulk_pull_blocks_mode) + sizeof (uint32_t)), [this_l](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_blocks_action (ec, size_a);
					});
					break;
				}
				case logos::message_type::frontier_req:
				{
					node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::frontier_req, logos::stat::dir::in);
					auto this_l (shared_from_this ()); // RGD Read all the bytes for logos::frontier_req
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 8, sizeof (logos::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t) + sizeof(uint64_t)), [this_l](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_frontier_req_action (ec, size_a);
					});
					break;
				}
				case logos::message_type::bulk_push:
				{
					node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::bulk_push, logos::stat::dir::in);
					add_request (std::unique_ptr<logos::message> (new logos::bulk_push));
					break;
				}
				default:
				{
                    std::cout << "RGDFIXME17 line: " << __LINE__ << " file: " << __FILE__ << std::endl;
					if (node->config.logging.network_logging ())
					{
						BOOST_LOG (node->log) << boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast<uint8_t> (type));
					}
					break;
				}
			}
		}
	}
	else
	{
		if (node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Error while receiving type: %1%") % ec.message ());
		}
	}
}

namespace
{
class request_response_visitor : public logos::message_visitor
{
public:
	request_response_visitor (std::shared_ptr<logos::bootstrap_server> connection_a) :
	connection (connection_a)
	{
	}
	virtual ~request_response_visitor () = default;
	void keepalive (logos::keepalive const &) override
	{
		assert (false);
	}
	void bulk_pull (logos::bulk_pull const &) override
	{
#ifdef _DEBUG
        std::cout << "request_response_visitor::bulk_pull" << std::endl;
#endif
        for(int i = 0; i < connection->requests.size(); ++i) {
            std::unique_ptr<logos::bulk_pull> pull (static_cast<logos::bulk_pull *> (connection->requests.front ().release ()));
            if(pull != nullptr) {
                std::cout << "RGDFIXME99 delegate_id<0>: " << pull->delegate_id << std::endl;
                auto response(std::make_shared<logos::bulk_pull_server>(connection,std::move(pull)));
		        response->send_next ();
                break;
            } else {
                std::cout << "RGDFIXME99 error<0> size: " << connection->requests.size() << std::endl;
            }
        }
#if 0
		auto response (std::make_shared<logos::bulk_pull_server> (connection, std::unique_ptr<logos::bulk_pull> (static_cast<logos::bulk_pull *> (connection->requests.front ().release ()))));
		response->send_next ();
#endif
	}
	void bulk_pull_blocks (logos::bulk_pull_blocks const &) override
	{
#ifdef _DEBUG
        std::cout << "request_response_visitor::bulk_pull_blocks" << std::endl;
#endif
#if 0
		auto response (std::make_shared<logos::bulk_pull_blocks_server> (connection, std::unique_ptr<logos::bulk_pull_blocks> (static_cast<logos::bulk_pull_blocks *> (connection->requests.front ().release ()))));
		response->send_next ();
#endif
	}
	void bulk_push (logos::bulk_push const &) override
	{
		auto response (std::make_shared<logos::bulk_push_server> (connection));
		response->receive ();
	}
	void frontier_req (logos::frontier_req const &) override
	{
		auto response (std::make_shared<logos::frontier_req_server> (connection, std::unique_ptr<logos::frontier_req> (static_cast<logos::frontier_req *> (connection->requests.front ().release ()))));
		response->send_next ();
	}
	std::shared_ptr<logos::bootstrap_server> connection;
};
}

void logos::bootstrap_server::receive_bulk_pull_action (boost::system::error_code const & ec, size_t size_a)
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::receive_bulk_pull_action" << std::endl;
#endif
	if (!ec)
	{
		std::unique_ptr<logos::bulk_pull> request (new logos::bulk_pull);
		logos::bufferstream stream (receive_buffer.data (), 8 + logos::bulk_pull::SIZE);
		auto error (request->deserialize (stream));
        // RGD Hack
		std::unique_ptr<logos::bulk_pull> request1 (new logos::bulk_pull);
		logos::bufferstream stream1 (receive_buffer.data (), 8 + logos::bulk_pull::SIZE);
		auto error1 (request->deserialize (stream1));
		if (!error)
		{
            std::cout << "RGDFIXME99 line: " << __LINE__ << " file: " << __FILE__ << " delegate_id: " << request->delegate_id << std::endl;
			if (node && node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received bulk pull for %1% down to %2%") % request->start.to_string () % request->end.to_string ());
			}
	        std::lock_guard<std::mutex> lock (mutex);
            requests.push(std::move(std::unique_ptr<logos::message>(request.release())));
            request_response_visitor visitor(shared_from_this());
            request1->visit(visitor); // RGD Hack Force it
			receive ();
#if 0
			add_request (std::unique_ptr<logos::message> (request.release ()));
			receive ();
#endif
		} else {
#ifdef _DEBUG
            std::cout << "logos::bootstrap_server::receive_bulk_pull_action:: error deserializing request" << std::endl;    
#endif
        }
	}
}

void logos::bootstrap_server::receive_bulk_pull_blocks_action (boost::system::error_code const & ec, size_t size_a)
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::receive_bulk_pull_blocks_action" << std::endl;
#endif
	if (!ec)
	{
		std::unique_ptr<logos::bulk_pull_blocks> request (new logos::bulk_pull_blocks);
		logos::bufferstream stream (receive_buffer.data (), 8 + sizeof (logos::uint256_union) + sizeof (logos::uint256_union) + sizeof (bulk_pull_blocks_mode) + sizeof (uint32_t));
		auto error (request->deserialize (stream));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received bulk pull blocks for %1% to %2%") % request->min_hash.to_string () % request->max_hash.to_string ());
			}
			add_request (std::unique_ptr<logos::message> (request.release ()));
			receive ();
		}
	}
}

void logos::bootstrap_server::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a)
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::receive_frontier_req_action" << std::endl;
#endif
	if (!ec)
	{
		std::unique_ptr<logos::frontier_req> request (new logos::frontier_req);
        // RGD Should it be sizeof(logos::frontier_req) ?
		logos::bufferstream stream (receive_buffer.data (), 8 + sizeof (logos::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t) + sizeof(uint64_t));
		auto error (request->deserialize (stream));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (node->log) << boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age);
			}
			add_request (std::unique_ptr<logos::message> (request.release ()));
			receive (); // RGDTOOD This looks like where it handles the request.
		}
	}
	else
	{
		if (node->config.logging.network_logging ())
		{
			BOOST_LOG (node->log) << boost::str (boost::format ("Error sending receiving frontier request: %1%") % ec.message ());
		}
	}
}

void logos::bootstrap_server::add_request (std::unique_ptr<logos::message> message_a)
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::add_request: " << (message_a==nullptr) << std::endl;
#endif
	std::lock_guard<std::mutex> lock (mutex);
	auto start (requests.empty ());
	requests.push (std::move (message_a)); // RGDTODO The request for frontier is added here.
	if (start)
	{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::add_request: run_next:" << std::endl;
#endif
		run_next ();
	}
    else
    {
#ifdef _DEBUG
    std::cout << "RGDFIXME99 logos::bootstrap_server::add_request: start: failed: " << start << std::endl;
#endif
    }
}

void logos::bootstrap_server::finish_request ()
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::finish_request" << std::endl;
#endif
	std::lock_guard<std::mutex> lock (mutex);
	requests.pop ();
	if(!requests.empty ())
	{
#ifdef _DEBUG
        std::cout << "logos::bootstrap_server::finish_request: run_next:" << std::endl;
#endif
		run_next ();
	}
    else 
    {
#ifdef _DEBUG
        std::cout << "RGDFIXME logos::bootstrap_server::finish_request: run_next: empty" << std::endl;
#endif
    }
}



void logos::bootstrap_server::run_next ()
{
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::run_next: requests: " << (&(requests)) << " front: " << (&(requests.front())) << " empty: " << requests.empty() << std::endl;
#endif
	assert (!requests.empty ());
	request_response_visitor visitor (shared_from_this ());
    if((&(requests.front())) != nullptr)
    {
#ifdef _DEBUG
        if(!socket->is_open()) {
            std::cout << "logos::bootstrap_server::socket closed" << std::endl;
        }
#endif
	    requests.front()->visit (visitor);
    } else {
#ifdef _DEBUG
    std::cout << "logos::bootstrap_server::run_next: null front" << std::endl;
#endif
    }
}
