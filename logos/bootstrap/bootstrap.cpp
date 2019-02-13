#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>
#include <logos/bootstrap/tips.hpp>
#include <logos/bootstrap/batch_block_tips.hpp>
#include <logos/bootstrap/p2p.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>
#include <logos/lib/trace.hpp>

std::atomic<int> total_pulls;

#undef _DEBUG

#define _MODIFY_BUFFER 1
#define RECV_BUFFER_SIZE (1 << 20)
#define _ERROR 1

#ifdef _DEBUG
static std::atomic<int> open_count;
static std::atomic<int> server_open_count;
static std::atomic<int> close_count;
#endif

static Log *log_g = nullptr;
std::deque<logos::pull_info> logos::bootstrap_attempt::dpulls;

Log &logos::bootstrap_get_logger()
{
    if(log_g) {
        return *log_g;
    } else {
        std::cout << "logos::bootstrap_get_logger:: using default logger" << std::endl;
        static Log log;
        return log;
    }
}

void logos::bootstrap_init_logger(Log *log)
{
    log_g = log;
}

logos::socket_timeout::socket_timeout (logos::bootstrap_client & client_a) :
ticket (0),
client (client_a)
{
}

void logos::socket_timeout::start (std::chrono::steady_clock::time_point timeout_a)
{
    auto ticket_l (++ticket);
    std::weak_ptr<logos::bootstrap_client> client_w (client.shared ());
    client.node->alarm.add (timeout_a, [client_w, ticket_l]() { // NOTE: If we timeout, we disconnect.
        if (auto client_l = client_w.lock ())
        {
            if (client_l->timeout.ticket == ticket_l)
            {
                LOG_DEBUG(logos::bootstrap_get_logger()) << "logos::socket_timeout::start: socket->close" << std::endl;
#ifdef _DEBUG
                close_count++;
#endif
                client_l->socket.close ();
                if (client_l->node->config.logging.bulk_pull_logging ())
                {
                    LOG_INFO (client_l->node->log) << boost::str (boost::format ("Disconnecting from %1% due to timeout") % client_l->socket.remote_endpoint ());
                }
            }
        }
    });
}

void logos::socket_timeout::stop ()
{
    LOG_DEBUG(logos::bootstrap_get_logger()) << "logos::socket_timeout::stop:" << std::endl;
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
        ++attempt->connections; // NOTE: Number of connection attempts.
}

logos::bootstrap_client::~bootstrap_client ()
{
    if(attempt)
        --attempt->connections;
    LOG_DEBUG(node->log) << "logos::bootstrap_client::~bootstrap_client" << std::endl;
    socket.close();
}

double logos::bootstrap_client::block_rate () const
{
    auto elapsed = elapsed_seconds ();
    return elapsed > 0.0 ? (double)block_count.load () / elapsed : 0.0; // NOTE: Rate at which we recieve blocks.
}

double logos::bootstrap_client::elapsed_seconds () const
{
    return std::chrono::duration_cast<std::chrono::duration<double>> (std::chrono::steady_clock::now () - start_time).count ();
}

void logos::bootstrap_client::stop (bool force)
{
    LOG_DEBUG(node->log) << "logos::bootstrap_client::stop:" << std::endl;
#ifdef _DEBUG
    std::cout << "trace: " << __FILE__ << " line: " << __LINE__ << std::endl;
    trace();
#endif

    pending_stop = true;
    if (force)
    {
        hard_stop = true;
    }
}

void logos::bootstrap_client::start_timeout ()
{
    timeout.start (std::chrono::steady_clock::now () + std::chrono::seconds (20)); // NOTE: Set a timeout on the connection.
}

void logos::bootstrap_client::stop_timeout ()
{
    timeout.stop ();
}

void logos::bootstrap_client::run ()
{
    auto this_l (shared_from_this ());
    start_timeout ();
    socket.async_connect (endpoint, [this_l](boost::system::error_code const & ec) { // NOTE: endpoint is passed into the constructor of bootstrap_client, attempt to connect.
        this_l->stop_timeout ();
        if (!ec)
        {
#ifdef _MODIFY_BUFFER
            try {
                boost::asio::socket_base::send_buffer_size option1(RECV_BUFFER_SIZE);
                this_l->socket.set_option(option1);
                boost::asio::socket_base::receive_buffer_size option2(RECV_BUFFER_SIZE);
                this_l->socket.set_option(option2);
            } 
            // NOTE: These exceptions are ok, just means we couldn't set the size, but there is a default.
            catch(const boost::system::system_error& e)
            {
                LOG_DEBUG(this_l->node->log) << "exception while setting socket option: " << e.what() << std::endl;
            }
            catch(...) 
            {
                LOG_DEBUG(this_l->node->log) << "unknown exception while setting socket option" << std::endl;
            }
#endif
            if (this_l->node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (this_l->node->log) << boost::str (boost::format ("Connection established to %1%") % this_l->endpoint);
            }
            
#ifdef _DEBUG
            std::cout << "trace: " << __FILE__ << " line: " << __LINE__ << std::endl;
            trace();
#endif
            LOG_DEBUG(this_l->node->log) << "logos::bootstrap_client::run: pool_connection called" << std::endl;
            this_l->attempt->pool_connection (this_l->shared_from_this ()); // NOTE: Add connection, updates idle queue.
        }
        else
        {
            LOG_DEBUG(this_l->node->log) << "logos::bootstrap_client::run: network error: ec.message: " << ec.message() << std::endl;
            if (this_l->node->config.logging.network_logging ())
            {
                switch (ec.value ())
                {
                    default:
                        LOG_INFO (this_l->node->log) << boost::str (boost::format ("Error initiating bootstrap connection to %1%: %2%") % this_l->endpoint % ec.message ());
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
get_next_micro (0),
stopped (false)
{
    logos::bootstrap_init_logger(&node->log); // Store our logger for later use.
    LOG_INFO (node->log) << "Starting bootstrap attempt";
    node->bootstrap_initiator.notify_listeners (true);
    session_id = p2p::get_peers();
}

logos::bootstrap_attempt::~bootstrap_attempt ()
{
    LOG_INFO (node->log) << "Exiting bootstrap attempt";
    stop();
    node->bootstrap_initiator.notify_listeners (false);
    p2p::close_session(session_id);
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

bool logos::bootstrap_attempt::request_tips(std::unique_lock<std::mutex> & lock_a) // NOTE: Get the connection from the pool (see 'connection') and get tips.
{
    auto result (true);
    auto connection_l (connection (lock_a));
    connection_tips_request = connection_l;
    if (connection_l)
    {
        std::future<bool> future;
        {
            auto client (std::make_shared<logos::tips_req_client> (connection_l));
            client->run (); // NOTE: Call tips_req_client::run.
            tips = client;
            future = client->promise.get_future ();
        }
        lock_a.unlock ();
        result = consume_future (future);
        LOG_DEBUG(node->log) << "logos::bootstrap_attempt::request_tips : future: " << result << std::endl;
        lock_a.lock ();
        if (result)
        {
            LOG_DEBUG(node->log) << "logos::bootstrap_attempt::request_tips : clearing pulls: " << pulls.size() << std::endl;
            pulls.clear ();
        }
        if (node->config.logging.network_logging ())
        {
            if (!result)
            {
                LOG_INFO (node->log) << boost::str (boost::format ("Completed tips request, %1% out of sync accounts according to %2%") % pulls.size () % connection_l->endpoint);
            }
            else
            {
                LOG_INFO (node->log) << "tips_req failed, reattempting";
            }
        }
    }
    return result;
}

void logos::bootstrap_attempt::request_pull (std::unique_lock<std::mutex> & lock_a)
{ // NOTE: Called form 'logos::bootstrap_attempt::run'
    LOG_DEBUG(node->log) << "logos::bootstrap_attempt::request_pull: start" << std::endl;
    auto connection_l (connection (lock_a));
    if (connection_l)
    {
        auto pull (pulls.front ());
        for(int i = 0; i < pulls.size(); ++i) {
            LOG_DEBUG(node->log) << "logos::bootstrap_attempt::request_pull: pull: " << pulls[i].delegate_id << std::endl;
        }
        LOG_DEBUG(node->log) << "pulls.pop_front()" << std::endl;
        pulls.pop_front ();
        auto size (pulls.size ());
        // The bulk_pull_client destructor attempt to requeue_pull which can cause a deadlock if this is the last reference
        // Dispatch request in an external thread in case it needs to be destroyed
        node->background ([connection_l, pull]() {
            auto client (std::make_shared<logos::bulk_pull_client> (connection_l, pull));
            if(pull.type == pull_type::batch_block_pull) {
                LOG_DEBUG(connection_l->node->log) << "logos::bootstrap_attempt::request_pull: client->request_batch_block: delegate_id: " << pull.delegate_id << std::endl;
                client->request_batch_block(); // NOTE: Call 'bulk_pull_client::request_batch_block'
            }
        });
    }
}

bool logos::bootstrap_attempt::still_pulling ()
{
    assert (!mutex.try_lock ());
    auto running (!stopped);
    auto more_pulls (!pulls.empty ());
    auto still_pulling (pulling > 0);
    LOG_DEBUG(node->log) << "logos::bootstrap_attempt::still_pulling: running: " << running << " more_pulls: " << more_pulls << " still_pulling: " << still_pulling << std::endl;
    return running && (more_pulls || still_pulling);
}

void logos::bootstrap_attempt::run ()
{
    LOG_DEBUG(node->log) << "bootstrap_attempt::run begin {" << std::endl;
    populate_connections ();
    std::unique_lock<std::mutex> lock (mutex);
    auto tips_failure (true);
    uint32_t retry = 0;
    while (!stopped && tips_failure)
    {
        retry++;
        if(retry > bootstrap_max_retry) {
            LOG_FATAL(node->log) << "logos::bootstrap_attempt::run error too many retries for request_tips" << std::endl;
            trace();
            break; // Couldn't get tips from this peer...
        }
        tips_failure = request_tips(lock);
    }
    // Shuffle pulls.
    for (int i = pulls.size () - 1; i > 0; i--)
    {
        auto k = logos::random_pool.GenerateWord32 (0, i);
        std::swap (pulls[i], pulls[k]);
    }
    while (still_pulling ())
    {
        while (still_pulling ())
        {
            LOG_DEBUG(node->log) << "logos::pulling:: total_pulls: " << total_pulls << std::endl;
            if (!pulls.empty () && total_pulls <= 32)
            {
                request_pull (lock); // NOTE: Start of bulk_pull_client.
            }
            else
            {
                LOG_DEBUG(node->log) << "wait..." << std::endl;
                condition.wait (lock);
            }
        }
    }
    if (!stopped)
    {
        LOG_INFO (node->log) << "Completed pulls";
    }
    stopped = true;
    condition.notify_all ();
    for(int i = 0; i < idle.size(); ++i) {
        idle[i]->socket.close();
    }
    idle.clear (); // Must wait till threads using this have stopped, else mem fault...

    LOG_DEBUG(node->log) << "bootstrap_attempt::run end }" << std::endl;
}

std::shared_ptr<logos::bootstrap_client> logos::bootstrap_attempt::connection (std::unique_lock<std::mutex> & lock_a)
{
    while (!stopped && idle.empty ())
    {
        // The code has been observed to wait here if idle is empty...
        // First we create connections in populate_connection, then those clients call run and
        // if they successfully connect, the new connection goes into the idle queue. If we have
        // no connections, we need to wait for them.
        LOG_DEBUG(node->log) << "logos::bootstrap_attempt::connection:: wait... !idle.empty(): " << !idle.empty() << " stopped: " << stopped << std::endl;
        condition.wait (lock_a);
    }
    std::shared_ptr<logos::bootstrap_client> result;
    if (!idle.empty ())
    {
        result = idle.back ();
        idle.pop_back ();
    } else {
        LOG_DEBUG(node->log) << "idle is empty: stopped: " << stopped << " !idle.empty(): " << !(idle.empty()) << std::endl;
    }
    return result;
}

bool logos::bootstrap_attempt::consume_future (std::future<bool> & future_a)
{
    bool result;
    try
    {
        std::chrono::system_clock::time_point minute_passed
            = std::chrono::system_clock::now() + std::chrono::seconds(60);
        if(std::future_status::ready == future_a.wait_until(minute_passed)) {
            result = future_a.get ();
        } else {
            // future timed out, return error.
            LOG_DEBUG(node->log) << "logos::bootstrap_attempt::consume_future: timeout" << std::endl;
            result = true;
        }
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
                LOG_INFO (node->log) << boost::str (boost::format ("Resolving fork between our block: %1% and block %2% both with root %3%") % ledger_block->hash ().to_string () % block_a->hash ().to_string () % block_a->root ().to_string ());
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
    LOG_DEBUG(node->log) << "bootstrap_attempt::populate_connections begin {" << std::endl;
    double rate_sum = 0.0;
    size_t num_pulls = 0;
    std::priority_queue<std::shared_ptr<logos::bootstrap_client>, std::vector<std::shared_ptr<logos::bootstrap_client>>, block_rate_cmp> sorted_connections;
    {
        std::unique_lock<std::mutex> lock (mutex);
        num_pulls = pulls.size ();
        LOG_DEBUG(node->log) << "bootstrap_attempt:: num_pulls: " << num_pulls << std::endl;
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
                        LOG_INFO (node->log) << boost::str (boost::format ("Stopping slow peer %1% (elapsed sec %2%s > %3%s and %4% blocks per second < %5%)") % client->endpoint.address ().to_string () % elapsed_sec % bootstrap_minimum_termination_time_sec % blocks_per_sec % bootstrap_minimum_blocks_per_sec);
                    }

                    LOG_DEBUG(node->log) << "bootstrap_attempt: client->stop <1>" << std::endl;
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
            LOG_INFO (node->log) << boost::str (boost::format ("Dropping %1% bulk pull peers, target connections %2%") % drop % target);
        }

        for (int i = 0; i < drop; i++)
        {
            auto client = sorted_connections.top ();

            if (node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (node->log) << boost::str (boost::format ("Dropping peer with block rate %1%, block count %2% (%3%) ") % client->block_rate () % client->block_count % client->endpoint.address ().to_string ());
            }

            LOG_DEBUG(node->log) << "bootstrap_attempt: client->stop <2>" << std::endl;
            client->stop (false);
            sorted_connections.pop ();
        }
    }

    if (node->config.logging.bulk_pull_logging ())
    {
        std::unique_lock<std::mutex> lock (mutex);
        LOG_INFO (node->log) << boost::str (boost::format ("Bulk pull connections: %1%, rate: %2% blocks/sec, remaining account pulls: %3%, total blocks: %4%") % connections.load () % (int)rate_sum % pulls.size () % (int)total_blocks.load ());
    }

    if (connections < target)
    {
        auto delta = std::min ((target - connections) * 2, bootstrap_max_new_connections);
        // Not many peers respond, need to try to make more connections than we need.
        // delta = NUMBER_DELEGATES; // Maybe set to 0 of clients is too big ?
        delta = 1; // delta of 1 seems to work the best in testing...

        LOG_DEBUG(node->log) << "bootstrap_attempt:: delta: " << delta << " target: " << target << " connections: " << connections << " max: " << bootstrap_max_new_connections << " clients.size: " << clients.size() << std::endl;

        for (int i = 0; i < delta; i++)
        {
            auto peer (node->peers.bootstrap_peer ());
            auto address = peer.address();
            // Check if we are in the blacklist and retry if it is until we get one that isn't
            // If we run out of peers, we shut this bootstrap_attempt down and retry later...
            int retry = 0;
            while(p2p::is_blacklisted(peer)) {
                ++retry;
                if(retry >= p2p::MAX_BLACKLIST_RETRY) {
                    // Shutdown this attempt, we failed too many times...
                    stopped = true;
                    condition.notify_all ();
                    return;
                }
                peer = node->peers.bootstrap_peer (); // Get another peer.
                address = peer.address(); 
            }
            // ok to proceed, not blacklisted...
            p2p::add_peer(peer);
            if(address.to_v6().to_string() == std::string("::ffff:172.1.1.100")) {
                continue; // RGD Hack TODO remove this...
            }

            if (peer != logos::endpoint (boost::asio::ip::address_v6::any (), 0))
            {
#ifdef _DEBUG
                open_count++;
#endif
                auto client (std::make_shared<logos::bootstrap_client> (node, shared_from_this (), logos::tcp_endpoint (peer.address (), BOOTSTRAP_PORT )));
                client->run ();
                std::lock_guard<std::mutex> lock (mutex);
                clients.push_back (client);
            }
            else if (connections == 0)
            {
                LOG_INFO (node->log) << boost::str (boost::format ("Bootstrap stopped because there are no peers"));
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
    LOG_DEBUG(node->log) << "bootstrap_attempt::populate_connections end }" << std::endl;
}

void logos::bootstrap_attempt::add_connection (logos::endpoint const & endpoint_a)
{
#ifdef _DEBUG
    open_count++;
#endif
    auto client (std::make_shared<logos::bootstrap_client> (node, shared_from_this (), logos::tcp_endpoint (endpoint_a.address (), endpoint_a.port ())));
    client->run ();
}

void logos::bootstrap_attempt::pool_connection (std::shared_ptr<logos::bootstrap_client> client_a)
{
    std::lock_guard<std::mutex> lock (mutex);
    idle.push_front (client_a);
    LOG_DEBUG(node->log) << "logos::bootstrap_attempt::pool_connection !idle.empty(): " << !idle.empty() << std::endl;
    condition.notify_all ();
}

void logos::bootstrap_attempt::stop ()
{
    LOG_DEBUG(node->log) << "logos::bootstrap_attempt::stop" << std::endl;
    std::lock_guard<std::mutex> lock (mutex);
    stopped = true;
    condition.notify_all ();
    for (auto i : clients)
    {
        if (auto client = i.lock ())
        {
            LOG_DEBUG(node->log) << "logos::bootstrap_attempt::stop: socket->close" << std::endl;
#ifdef _DEBUG
            close_count++;
#endif
            client->socket.close ();
        }
    }
    if (auto i = tips.lock ())
    {
        try
        {
            i->promise.set_value (true);
        }
        catch (std::future_error &e)
        {
            LOG_DEBUG(node->log) << "logos::bootstrap_attempt::stop caught error in setting promise: " << e.what() << std::endl;
        }
    }
}

void logos::bootstrap_attempt::add_pull (logos::pull_info const & pull)
{
    std::unique_lock<std::mutex> lock (mutex);

    LOG_DEBUG(node->log) << "logos::bootstrap_attempt::add_pull: " << pull.delegate_id << std::endl;

    pulls.push_back (pull);
    condition.notify_all ();
}

void logos::bootstrap_attempt::add_defered_pull(logos::pull_info const &pull)
{
    std::unique_lock<std::mutex> lock (mutex);
    //get_next_micro = 1;
    LOG_DEBUG(node->log) << "logos::bootstrap_attempt::add_defered_pull: " << pull.delegate_id << std::endl;
    dpulls.push_back(pull);
}


void logos::bootstrap_attempt::run_defered_pull()
{
    std::unique_lock<std::mutex> lock (mutex);
    LOG_DEBUG(node->log) << "logos::bootstrap_attempt::run_defered_pull: " << std::endl;
    auto iter = dpulls.begin();
    while(iter != dpulls.end()) {
        pulls.push_back(*iter);
        condition.notify_all ();
        iter++;
    }
    dpulls.clear();
}

void logos::bootstrap_attempt::add_pull_bsb  (uint64_t start, uint64_t end, uint64_t seq_start, uint64_t seq_end, int delegate_id, BlockHash b_start, BlockHash b_end)
{
    BlockHash zero = 0;
    add_defered_pull(logos::pull_info(start,end,seq_start,seq_end,delegate_id,zero,zero,zero,zero,b_start,b_end));
}

void logos::bootstrap_attempt::add_pull_micro(uint64_t start, uint64_t end, uint64_t seq_start, uint64_t seq_end, BlockHash m_start, BlockHash m_end)
{
    BlockHash zero = 0;
    add_defered_pull(logos::pull_info(start,end,seq_start,seq_end,0,zero,zero,m_start,m_end,zero,zero));
}

void logos::bootstrap_attempt::add_pull_epoch(uint64_t start, uint64_t end, uint64_t seq_start, uint64_t seq_end, BlockHash e_start, BlockHash e_end)
{
    BlockHash zero = 0;
    add_defered_pull(logos::pull_info(start,end,seq_start,seq_end,0,e_start,e_end,zero,zero,zero,zero));
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
            attempt->run (); // NOTE Call bootstrap_attempt::run
            lock.lock ();
            attempt = nullptr; // stop is called in destructor...
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
    LOG_DEBUG(node.log) << "port_a: " << port_a << std::endl;
}

void logos::bootstrap_listener::start ()
{
    //acceptor.open (local.protocol ());
    acceptor.open (endpoint().protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

    boost::system::error_code ec;
    //acceptor.bind (local, ec); // RGD
    acceptor.bind(endpoint(), ec);
    if (ec)
    {
        LOG_INFO (node.log) << boost::str (boost::format ("Error while binding for bootstrap on port %1%: %2%") % local.port () % ec.message ());
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
    LOG_DEBUG(node.log) << "logos::bootstrap_listener::stop: acceptor->close" << std::endl;
#ifdef _DEBUG
    close_count++;
#endif
    acceptor.close ();
    for (auto & i : connections_l)
    {
        auto connection (i.second.lock ());
        if (connection)
        {
            LOG_DEBUG(node.log) << "logos::bootstrap_listener::stop: socket->close" << std::endl;
#ifdef _DEBUG
            close_count++;
#endif
            connection->socket->close ();
        }
    }
}

void logos::bootstrap_listener::accept_connection ()
{
    auto socket (std::make_shared<boost::asio::ip::tcp::socket> (service));
    //std::shared_ptr<boost::asio::ip::tcp::socket> socket (new boost::asio::ip::tcp::socket(service,endpoint()));
    //auto end = boost::asio::ip::tcp::endpoint 
    //    (boost::asio::ip::address_v6::from_string(std::string("::ffff:") + (node.config.consensus_manager_config.local_address)));
    //std::shared_ptr<boost::asio::ip::tcp::socket> socket (new boost::asio::ip::tcp::socket(service,end));

#ifdef _MODIFY_BUFFER
    try {
        boost::asio::socket_base::send_buffer_size option1(RECV_BUFFER_SIZE);
        socket->set_option(option1);
        boost::asio::socket_base::receive_buffer_size option2(RECV_BUFFER_SIZE);
        socket->set_option(option2);
    } 
    //NOTE: These exceptions are ok, just means we couldn't set the size, but there is a default.
    catch(const boost::system::system_error& e)
    {
        LOG_DEBUG(node.log) << "exception while setting socket option: " << e.what() << std::endl;
    }
    catch(...) 
    {
        LOG_DEBUG(node.log) << "unknown exception while setting socket option" << std::endl;
    }
#endif
    acceptor.async_accept (*socket, [this, socket](boost::system::error_code const & ec) {
#ifdef _DEBUG
        server_open_count++;
        open_count++;
#endif
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
#ifdef _ERROR
            std::unique_lock<std::mutex> lock (mutex);
            if (connections.size () < node.config.bootstrap_connections_max && acceptor.is_open ())
#else
            std::lock_guard<std::mutex> lock (mutex);
            if (connections.size () < 8192 && acceptor.is_open ())
#endif
            {
                LOG_DEBUG(node.log) << "logos::bootstrap_listener::accept_action: " << connections.size() << " acceptor.is_open(): " << acceptor.is_open() << std::endl;
                connections[connection.get ()] = connection;
                connection->receive ();
            } else {
                LOG_DEBUG(node.log) << "logos::bootstrap_listener::accept_action: error " << connections.size() << " acceptor.is_open(): " << acceptor.is_open() << std::endl;
                /* 
                lock.unlock();
                stop();
                lock.lock();
                start();
                */
            }
        }
    }
    else
    {
        LOG_INFO (node.log) << boost::str (boost::format ("Error while accepting bootstrap connections: %1%") % ec.message ());
        LOG_DEBUG(node.log) << "logos::bootstrap_listener::accept_action: networking error: ec.message: " << ec.message() << std::endl;
#ifdef _DEBUG
        LOG_DEBUG(node.log) << " open: " << open_count << " closed: " << close_count << " server_open_count: " << server_open_count << " client open count: " << (open_count-server_open_count) << std::endl;
#endif
    }
}

boost::asio::ip::tcp::endpoint logos::bootstrap_listener::endpoint ()
{
    return boost::asio::ip::tcp::endpoint 
        (boost::asio::ip::address_v6::from_string(std::string("::ffff:") + (node.config.consensus_manager_config.local_address)), 
        BOOTSTRAP_PORT);
        
}

logos::bootstrap_server::~bootstrap_server ()
{
    if (node->config.logging.bulk_pull_logging ())
    {
        LOG_INFO (node->log) << "Exiting bootstrap server";
    }
    std::lock_guard<std::mutex> lock (node->bootstrap.mutex);
    LOG_DEBUG(node->log) << "logos::bootstrap_server::~bootstrap_server" << std::endl;
#ifdef _DEBUG
    std::cout << "trace: " << __FILE__ << " line: " << __LINE__ << std::endl;
    trace();
#endif
    socket->close();
    node->bootstrap.connections.erase (this);
}

logos::bootstrap_server::bootstrap_server (std::shared_ptr<boost::asio::ip::tcp::socket> socket_a, std::shared_ptr<logos::node> node_a) :
socket (socket_a),
node (node_a)
{
    LOG_DEBUG(node->log) << "logos::bootstrap_server::bootstrap_server: " << node << std::endl;
}

void logos::bootstrap_server::receive ()
{
    LOG_DEBUG(node->log) << "logos::bootstrap_server::receive" << std::endl;
    auto this_l (shared_from_this ());
    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), bootstrap_header_size), [this_l](boost::system::error_code const & ec, size_t size_a) {
        this_l->receive_header_action (ec, size_a);
    });
}

void logos::bootstrap_server::receive_header_action (boost::system::error_code const & ec, size_t size_a)
{ // NOTE: Start of server request handling.
    LOG_DEBUG(node->log) << "logos::bootstrap_server::receive_header_action" << std::endl;
    if (!ec)
    {
        //assert (size_a == bootstrap_header_size);
        if(size_a != bootstrap_header_size) {
            return;
        }
        logos::bufferstream type_stream (receive_buffer.data (), size_a);
        uint8_t version_max;
        uint8_t version_using;
        uint8_t version_min;
        logos::message_type type;
        std::bitset<16> extensions;
        if (!logos::message::read_header (type_stream, version_max, version_using, version_min, type, extensions))
        {
            switch (type)
            {
                case logos::message_type::batch_blocks_pull:
                {
                    node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::bulk_pull, logos::stat::dir::in);
                    auto this_l (shared_from_this ());
                    boost::asio::async_read (*socket, boost::asio::buffer (
                        receive_buffer.data () + bootstrap_header_size, 
                        logos::bulk_pull::SIZE
                    ), [this_l](boost::system::error_code const & ec, size_t size_a) {
                        this_l->receive_bulk_pull_action (ec, size_a);
                    });
                    break;
                }
                case logos::message_type::bulk_pull:
                {
                    node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::bulk_pull, logos::stat::dir::in);
                    auto this_l (shared_from_this ());
                    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + bootstrap_header_size, sizeof (logos::uint256_union) + sizeof (logos::uint256_union)), [this_l](boost::system::error_code const & ec, size_t size_a) {
                        this_l->receive_bulk_pull_action (ec, size_a);
                    });
                    break;
                }
                case logos::message_type::frontier_req:
                {
                    node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::frontier_req, logos::stat::dir::in);
                    auto this_l (shared_from_this ()); // NOTE Read all the bytes for logos::frontier_req
                    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + bootstrap_header_size, sizeof (logos::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t) + sizeof(uint64_t) + BatchBlock::tips_response_mesg_len), [this_l](boost::system::error_code const & ec, size_t size_a) {
                        this_l->receive_tips_req_action (ec, size_a);
                    });
                    break;
                }
                default:
                {
                    LOG_DEBUG(node->log) << " Received invalid type from bootstrap connection " << std::endl;
                    if (node->config.logging.network_logging ())
                    {
                        LOG_INFO (node->log) << boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast<uint8_t> (type));
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
            LOG_INFO (node->log) << boost::str (boost::format ("Error while receiving type: %1%") % ec.message ());
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
        LOG_DEBUG(logos::bootstrap_get_logger()) << "request_response_visitor::bulk_pull" << std::endl;
        auto response (std::make_shared<logos::bulk_pull_server> (connection, std::unique_ptr<logos::bulk_pull> (static_cast<logos::bulk_pull *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    void bulk_pull_blocks (logos::bulk_pull_blocks const &) override
    {
        LOG_DEBUG(logos::bootstrap_get_logger()) << "request_response_visitor::bulk_pull_blocks" << std::endl;
    }
    void bulk_push(logos::bulk_push const &) override
    {
        LOG_DEBUG(logos::bootstrap_get_logger()) << "request_response_visitor::bulk_push" << std::endl;
    }
    void frontier_req (logos::frontier_req const &) override
    {
        auto response (std::make_shared<logos::tips_req_server> (connection, std::unique_ptr<logos::frontier_req> (static_cast<logos::frontier_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    std::shared_ptr<logos::bootstrap_server> connection;
};
}

void logos::bootstrap_server::receive_bulk_pull_action (boost::system::error_code const & ec, size_t size_a)
{
    LOG_DEBUG(node->log) << "logos::bootstrap_server::receive_bulk_pull_action" << std::endl;
    if (!ec)
    {
        std::unique_ptr<logos::bulk_pull> request (new logos::bulk_pull);
        logos::bufferstream stream (receive_buffer.data (), bootstrap_header_size + logos::bulk_pull::SIZE);
        auto error (request->deserialize (stream));
        if (!error)
        {
            if (node && node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (node->log) << boost::str (boost::format ("Received bulk pull for %1% down to %2%") % request->start.to_string () % request->end.to_string ());
            }
            add_request (std::unique_ptr<logos::message> (request.release ()));
            receive ();
        } else {
            LOG_DEBUG(node->log) << "logos::bootstrap_server::receive_bulk_pull_action:: error deserializing request" << std::endl;    
        }
    }
}

void logos::bootstrap_server::receive_bulk_pull_blocks_action (boost::system::error_code const & ec, size_t size_a)
{
    LOG_DEBUG(node->log) << "logos::bootstrap_server::receive_bulk_pull_blocks_action" << std::endl;
    if (!ec)
    {
        std::unique_ptr<logos::bulk_pull_blocks> request (new logos::bulk_pull_blocks);
        logos::bufferstream stream (receive_buffer.data (), bootstrap_header_size + sizeof (logos::uint256_union) + sizeof (logos::uint256_union) + sizeof (bulk_pull_blocks_mode) + sizeof (uint32_t));
        auto error (request->deserialize (stream));
        if (!error)
        {
            if (node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (node->log) << boost::str (boost::format ("Received bulk pull blocks for %1% to %2%") % request->min_hash.to_string () % request->max_hash.to_string ());
            }
            add_request (std::unique_ptr<logos::message> (request.release ()));
            receive ();
        }
    }
}

void logos::bootstrap_server::receive_tips_req_action (boost::system::error_code const & ec, size_t size_a)
{
    LOG_DEBUG(node->log) << "logos::bootstrap_server::receive_tips_req_action" << std::endl;
    if (!ec)
    {
        std::unique_ptr<logos::frontier_req> request (new logos::frontier_req);
        // NOTE Should it be sizeof(logos::frontier_req) ?
        logos::bufferstream stream (receive_buffer.data (), bootstrap_header_size + sizeof (logos::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t) + sizeof(uint64_t) + BatchBlock::tips_response_mesg_len);
        auto error (request->deserialize (stream));
        if (!error)
        {
            if (node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (node->log) << boost::str (boost::format ("Received tips request for %1% with age %2%") % request->start.to_string () % request->age);
            }
            // NOTE Store the request for later processing.
            add_request (std::unique_ptr<logos::message> (request.release ()));
            receive ();
        }
    }
    else
    {
        if (node->config.logging.network_logging ())
        {
            LOG_INFO (node->log) << boost::str (boost::format ("Error sending receiving tips request: %1%") % ec.message ());
        }
    }
}

void logos::bootstrap_server::add_request (std::unique_ptr<logos::message> message_a)
{
    LOG_DEBUG(node->log) << "logos::bootstrap_server::add_request: " << (message_a==nullptr) << std::endl;
    std::lock_guard<std::mutex> lock (mutex);
    auto start (requests.empty ());
    requests.push (std::move (message_a)); // NOTE The request for tips is added here.
    if (start)
    {
        LOG_DEBUG(node->log) << "logos::bootstrap_server::add_request: run_next:" << std::endl;
        run_next ();
    }
}

void logos::bootstrap_server::finish_request ()
{
    LOG_DEBUG(node->log) << "logos::bootstrap_server::finish_request" << std::endl;

    std::lock_guard<std::mutex> lock (mutex);
    requests.pop ();
    if(!requests.empty ())
    {
        LOG_DEBUG(node->log) << "logos::bootstrap_server::finish_request: run_next:" << std::endl;
        run_next ();
    }
    else 
    {
        LOG_DEBUG(node->log) << "logos::bootstrap_server::finish_request: run_next: empty" << std::endl;
    }
}

void logos::bootstrap_server::run_next ()
{
    LOG_DEBUG(node->log) << "logos::bootstrap_server::run_next: requests: " << (&(requests)) << " front: " << (&(requests.front())) << " empty: " << requests.empty() << std::endl;
    assert (!requests.empty ());
    request_response_visitor visitor (shared_from_this ());
    if((&(requests.front())) != nullptr)
    {
        if(!socket->is_open()) {
            LOG_DEBUG(node->log) << "logos::bootstrap_server::socket closed" << std::endl;
        }
        requests.front()->visit (visitor);
    } else {
        LOG_DEBUG(node->log) << "logos::bootstrap_server::run_next: null front" << std::endl;
    }
}
