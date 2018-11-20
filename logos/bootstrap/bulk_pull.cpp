#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>

extern std::atomic<int> total_pulls;

logos::pull_info::pull_info () :
account (0),
end (0),
attempts (0),
timestamp_start (0),
timestamp_end (0),
seq_start(0),
seq_end(0),
delegate_id(-1),
m_start(0),
m_end(0),
b_start(0),
b_end(0),
type(pull_type::account_pull)
{
}

logos::pull_info::pull_info (logos::account const & account_a, logos::block_hash const & head_a, logos::block_hash const & end_a) :
account (account_a),
head (head_a),
end (end_a),
attempts (0),
timestamp_start (0),
timestamp_end (0),
seq_start(0),
seq_end(0),
delegate_id(-1),
m_start(0),
m_end(0),
b_start(0),
b_end(0),
type(pull_type::account_pull)
{
}

logos::pull_info::pull_info (uint64_t _start, uint64_t _end, uint64_t _seq_start, uint64_t _seq_end, int _delegate_id, BlockHash _e_start, BlockHash _e_end, BlockHash _m_start, BlockHash _m_end, BlockHash _b_start, BlockHash _b_end) :
attempts (0),
timestamp_start (_start),
timestamp_end (_end),
seq_start(_seq_start),
seq_end(_seq_end),
delegate_id(_delegate_id),
e_start(_e_start),
e_end(_e_end),
m_start(_m_start),
m_end(_m_end),
b_start(_b_start),
b_end(_b_end),
type(pull_type::batch_block_pull)
{
#ifdef _DEBUG
    std::cout << "logos::pull_info::pull_info: delegate_id: " << delegate_id << std::endl;
#endif
}

logos::bulk_pull_client::bulk_pull_client (std::shared_ptr<logos::bootstrap_client> connection_a, logos::pull_info const & pull_a) :
connection (connection_a),
pull (pull_a)
{
	std::lock_guard<std::mutex> mutex (connection->attempt->mutex);
	++connection->attempt->pulling;
	connection->attempt->condition.notify_all ();
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_client::bulk_pull_client:: total_pulls: " << total_pulls << std::endl;
#endif
    total_pulls++;
}

logos::bulk_pull_client::~bulk_pull_client ()
{
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_client::~bulk_pull_client" << std::endl;
#endif
	std::lock_guard<std::mutex> mutex (connection->attempt->mutex);
	--connection->attempt->pulling;
	connection->attempt->condition.notify_all ();
    if(total_pulls > 0) total_pulls--;
}

void logos::bulk_pull_client::request ()
{ // RGD: Called in 'logos::bootstrap_attempt::requeue_pull'
  // RGD: Calls 'bulk_pull_client::receive_block' which calls 'bulk_pull_client::received_type', which calls 'bulk_pull_client::received_block'
	expected    = pull.head;
	logos::bulk_pull req;
	req.start   = pull.account;
	req.end     = pull.end;

#ifdef _DEBUG
    std::cout << "logos::bulk_pull_client::request: " << req << std::endl;
#endif

	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		logos::vectorstream stream (*buffer);
		req.serialize (stream);
	}
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		std::unique_lock<std::mutex> lock (connection->attempt->mutex);
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("Requesting account %1% from %2%. %3% accounts in queue") % req.start.to_account () % connection->endpoint % connection->attempt->pulls.size ());
	}
	else if (connection->node->config.logging.network_logging () && connection->attempt->should_log ())
	{
		std::unique_lock<std::mutex> lock (connection->attempt->mutex);
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("%1% accounts in pull queue") % connection->attempt->pulls.size ());
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		if (!ec)
		{
			this_l->receive_block ();
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
#ifdef _DEBUG
                std::cout << "logos::bulk_pull_client::request : client network error : " << ec.message() << std::endl;
#endif
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error sending bulk pull request to %1%: to %2%") % ec.message () % this_l->connection->endpoint);
			}
            this_l->connection->socket.close();
            if(total_pulls > 0) total_pulls--;
            //this_l->connection->attempt->pool_connection (this_l->connection); // FIXME
		}
	});
}

void logos::bulk_pull_client::request_batch_block()
{
  // RGD: Called in 'logos::bootstrap_attempt::requeue_pull'
  // RGD: Calls 'bulk_pull_client::receive_block' which calls 'bulk_pull_client::received_type', which calls 'bulk_pull_client::received_block'
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_client::request_batch_block delegate_id: " << pull.delegate_id << std::endl;
#endif
	logos::bulk_pull    req;
    req.type            = logos::message_type::batch_blocks_pull;
	req.start           = pull.account;
	req.end             = pull.end;
	req.timestamp_start = pull.timestamp_start;
	req.timestamp_end   = pull.timestamp_end;
    req.delegate_id     = pull.delegate_id;
    req.seq_start       = pull.seq_start;
    req.seq_end         = pull.seq_end;
    req.e_start         = pull.e_start;
    req.e_end           = pull.e_end;
    req.m_start         = pull.m_start;
    req.m_end           = pull.m_end;
    req.b_start         = pull.b_start;
    req.b_end           = pull.b_end;

#ifdef _DEBUG
    std::cout << " logos::bulk_pull_client::request_batch_block::pull: " << req << std::endl;
#endif

    if(!req.e_end.is_zero()) {
        end_transmission = req.e_end;
    }
    if(!req.m_end.is_zero()) {
        end_transmission = req.m_end;
    }
    if(!req.b_end.is_zero()) {
        end_transmission = req.b_end;
    }

	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		logos::vectorstream stream (*buffer);
		req.serialize (stream); // RGD Serialize has been implemented to support new fields.
	}
    // Logging...
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		std::unique_lock<std::mutex> lock (connection->attempt->mutex);
		BOOST_LOG (connection->node->log) << "bulk_pull_client::request_batch_block start: " << req.timestamp_start 
                                          << " end: " << req.timestamp_end << " delegate_id: " << req.delegate_id;
	}
	else if (connection->node->config.logging.network_logging () && connection->attempt->should_log ())
	{
		std::unique_lock<std::mutex> lock (connection->attempt->mutex);
		BOOST_LOG (connection->node->log) << boost::str (boost::format ("%1% accounts in pull queue") % connection->attempt->pulls.size ());
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_client::request_batch_block async_write delegate_id: " << pull.delegate_id << std::endl;
#endif
	boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		if (!ec)
		{
			this_l->receive_block ();
		}
		else
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::request_batch_block bulk_pull_client: delegate_id: " << this_l->pull.delegate_id << " line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
#endif
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error sending bulk pull request to %1%: to %2%") % ec.message () % this_l->connection->endpoint);
			}
            this_l->connection->socket.close();
            if(total_pulls > 0) total_pulls--;
            //this_l->connection->attempt->pool_connection (this_l->connection); // FIXME
		}
	});
}

void logos::bulk_pull_client::receive_block ()
{
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_client::receive_block" << std::endl;
#endif

   	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		if (!ec)
		{
			this_l->received_type ();
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type: %1%") % ec.message ());
			}
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::receive_block: bulk_pull_client: delegate_id: " << this_l->pull.delegate_id << " line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
#endif
            this_l->connection->socket.close();
            if(total_pulls > 0) total_pulls--;
            //this_l->connection->attempt->pool_connection (this_l->connection); // FIXME
		}
	});
}

void logos::bulk_pull_client::received_type ()
{

#ifdef _DEBUG
    static int count = 0;
    std::cout << "logos::bulk_pull_client::received_type: invocation: " << count++ << " delegate_id: " << pull.delegate_id << std::endl;
#endif

	auto this_l (shared_from_this ());
	logos::block_type type (static_cast<logos::block_type> (connection->receive_buffer[0]));
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_client::received_type: " << (int)type << " delegate_id: " << pull.delegate_id << std::endl;
#endif
	switch (type)
	{
		case logos::block_type::batch_block:
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::received_type: logos::block_type::batch_block" << std::endl;
#endif
            //std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Half second throttle.
			connection->start_timeout ();
			boost::asio::async_read (connection->socket, boost::asio::buffer
                (connection->receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response) - 1),
                [this_l](boost::system::error_code const & ec, size_t size_a) {
				    this_l->connection->stop_timeout ();
				    this_l->received_block (ec, size_a);
			});
			break;
		}
		case logos::block_type::micro_block:
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::received_type: logos::block_type::micro_block" << std::endl;
#endif
			connection->start_timeout ();
			boost::asio::async_read (connection->socket, boost::asio::buffer 
                (connection->receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response_micro) - 1), 
                [this_l](boost::system::error_code const & ec, size_t size_a) {
				    this_l->connection->stop_timeout ();
				    this_l->received_block (ec, size_a);
			});
			break;
		}
		case logos::block_type::epoch_block:
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::received_type: logos::block_type::epoch_block" << std::endl;
#endif
			connection->start_timeout ();
			boost::asio::async_read (connection->socket, boost::asio::buffer 
                (connection->receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response_epoch) - 1), 
                [this_l](boost::system::error_code const & ec, size_t size_a) {
				    this_l->connection->stop_timeout ();
				    this_l->received_block (ec, size_a);
			});
			break;
		}
		case logos::block_type::state:
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::received_type: logos::block_type::state" << std::endl;
#endif
			connection->start_timeout ();
			boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data () + 1, logos::state_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->connection->stop_timeout ();
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case logos::block_type::not_a_block:
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::received_type: logos::block_type::not_a_block" << std::endl;
#endif
			// Avoid re-using slow peers, or peers that sent the wrong blocks.
#ifndef _DEBUG
			if (!connection->pending_stop && expected == end_transmission) // RESEARCH
			{
				connection->attempt->pool_connection (connection);
			}
#else
            connection->attempt->pool_connection (connection); // FIXME
#endif

            if(connection->node->_validator->validate(nullptr)) {
			    if (connection->node->config.logging.bulk_pull_logging ())
			    {
			        BOOST_LOG (connection->node->log) << " bulk_pull_client::received_block got invalid batch block ";
			    }
#ifndef _DEBUG
                connection->stop(true); // FIXME
#endif
            }
			break;
		}
		default:
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::received_type: default: received unknown type block: " << (int)type << " delegate_id: " << pull.delegate_id << std::endl;
#endif
			if (connection->node->config.logging.network_packet_logging ())
			{
				BOOST_LOG (connection->node->log) << boost::str (boost::format ("Unknown type received as block type: %1%") % static_cast<int> (type));
			}
			break;
		}
	}
}

void logos::bulk_pull_client::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
#ifdef _DEBUG
        static int count = 0;
        std::cout << "logos::bulk_pull_client::received_block: count: " << count++ << std::endl;
#endif
	    logos::block_type type (static_cast<logos::block_type> (connection->receive_buffer[0]));
        if(type == logos::block_type::batch_block) {
			uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response> block(new BatchBlock::bulk_pull_response);
            if(block) {
                memcpy(&(*block),data,sizeof(BatchBlock::bulk_pull_response));
                BlockHash hash = block->block.Hash();
#ifdef _DEBUG
                std::cout << "logos::bulk_pull_client::received_block batch block received: delegate_id: " << block->delegate_id << " "
                          << "r->Hash(): " << hash.to_string() << std::endl;
#endif
				if (connection->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (connection->node->log) << " bulk_pull_client::received_block got block hash " << hash.to_string();
				}
				if (connection->block_count++ == 0)
				{
					connection->start_time = std::chrono::steady_clock::now ();
				}
				connection->attempt->total_blocks++;

                if(connection->node->_validator->validate(block)) {
				    if (connection->node->config.logging.bulk_pull_logging ())
				    {
					    BOOST_LOG (connection->node->log) << " bulk_pull_client::received_block got invalid batch block " << hash.to_string();
				    }
#ifndef _DEBUG
                    connection->stop(true); // FIXME
#endif
                }
				if (!connection->hard_stop.load ()) {
#ifdef _DEBUG
                    std::cout << "logos::bulk_pull_client::received_block: calling receive_block: " << std::endl;
#endif
                    expected = hash;
					receive_block (); // RGD: Read more blocks. This implements a loop.
				} else {
                    connection->socket.close();
                    connection->stop(true); // FIXME
                    // RESEARCH Do we need to pool connection here ?
                }
            }
        } else if(type == logos::block_type::micro_block) {
			uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response_micro> block(new BatchBlock::bulk_pull_response_micro);
            if(block) {
                memcpy(&(*block),data,sizeof(BatchBlock::bulk_pull_response_micro));
                BlockHash hash = block->micro.Hash();
				if (connection->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (connection->node->log) << " bulk_pull_client::received_block got block hash " << hash.to_string();
				}
				if (connection->block_count++ == 0)
				{
					connection->start_time = std::chrono::steady_clock::now ();
				}
				connection->attempt->total_blocks++;

                connection->node->_validator->add_micro_block(block);

				if (!connection->hard_stop.load ()) {
#ifdef _DEBUG
                    std::cout << "logos::bulk_pull_client::received_block: calling receive_block: "<< std::endl;
#endif
                    expected = hash;
					receive_block (); // RGD: Read more blocks. This implements a loop.
				} else {
                    connection->socket.close();
                    connection->stop(true); // FIXME
                }
            }
        } else if(type == logos::block_type::epoch_block) {
			uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response_epoch> block(new BatchBlock::bulk_pull_response_epoch);
            if(block) {
                memcpy(&(*block),data,sizeof(BatchBlock::bulk_pull_response_epoch));
                BlockHash hash = block->epoch.Hash();
				if (connection->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (connection->node->log) << " bulk_pull_client::received_block got block hash " << hash.to_string();
				}
				if (connection->block_count++ == 0)
				{
					connection->start_time = std::chrono::steady_clock::now ();
				}
				connection->attempt->total_blocks++;

                // FIXME LOGICAL bootstrap... 
                //       Pass in connection to validator, we need the attempt class to schedule a pull.
                //       The question is will we deadlock ?
                //       I don't think we will deadlock, but we need to test it out.
                std::cout << " received_epoch: " << hash.to_string() << std::endl;
                connection->node->_validator->add_epoch_block(block);

				if (!connection->hard_stop.load ()) {
#ifdef _DEBUG
                    std::cout << "logos::bulk_pull_client::received_block: calling receive_block: epoch: "<< hash.to_string() << std::endl;
#endif
                    expected = hash;
					receive_block (); // RGD: Read more blocks. This implements a loop.
				} else {
                    std::cout << "logos::bulk_pull_client::received_block: calling stop: epoch: "<< hash.to_string() << std::endl;
                    connection->socket.close();
                    connection->stop(true); // FIXME
                }
            }
        } else {
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_client::received_block: error deserializing block delegate_id: " << pull.delegate_id << " line: " << __LINE__ << std::endl;
#endif
		    if (connection->node->config.logging.bulk_pull_logging ())
			{
			    BOOST_LOG (connection->node->log) << "Error deserializing block received from pull request";
			}
        }
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error bulk receiving block: %1%") % ec.message ());
		}
#ifdef _DEBUG
        std::cout << "logos::bulk_pull_client::received_block: receive error: bulk_pull_client: delegate_id: " << pull.delegate_id << " line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
#endif
        connection->socket.close();
        if(total_pulls > 0) total_pulls--;
        //connection->attempt->pool_connection (connection); // FIXME
	}
}

/**
 * Handle a request for the pull of all blocks (bsb,micro,epoch) associated with a delegate.
 */
void logos::bulk_pull_server::set_current_end ()
{ // RGD: Called from logos::bulk_pull_server::bulk_pull_server
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_server::set_current_end: delegate_id: " << request->delegate_id << " start current_bsb: " << request->b_start.to_string() << std::endl;
#endif

    // Setup current_micro and current_bsb for iterating micro and bsb blocks.
    current_epoch   = request->e_start;
    if(current_epoch == request->e_end && !current_epoch.is_zero())
    {
        while(true) {
           BlockHash previous = EpochBlock::getPrevEpochBlock(connection->node->store, request->delegate_id, current_epoch);
           if(previous.is_zero()) {
                break;
           }
           current_epoch = previous; // Walk backwards till the beginning...
        }
    }

    current_micro   = request->m_start;
    if(current_micro == request->m_end && !current_micro.is_zero())
    {
        while(true) {
           BlockHash previous = Micro::getPrevMicroBlock(connection->node->store, request->delegate_id, current_micro);
           if(previous.is_zero()) {
                break;
           }
           current_micro = previous; // Walk backwards till the beginning...
        }
    }
    
    current_bsb     = request->b_start;
    if(current_bsb == request->b_end && !current_bsb.is_zero())
    {
#ifdef _DEBUG
        std::cout << "logos::bulk_pull_server::set_current_end: walking back chain for delegate: " << request->delegate_id << std::endl;
#endif
        while(true) {
           //std::shared_ptr<BatchStateBlock> b = BatchBlock::readBatchStateBlock(connection->node->store, current_bsb); 
           //BatchStateBlock *b = BatchBlock::readBatchStateBlock(connection->node->store, current_bsb).get();
           BlockHash previous = BatchBlock::getPrevBatchStateBlock(connection->node->store, request->delegate_id, current_bsb);
           if(previous.is_zero()) {
#ifdef _DEBUG
                //std::cout << "logos::bulk_pull_server::set_current_end: found root of chain for delegate: " << request->delegate_id << " current_bsb: " << current_bsb.to_string() << " next: " << b->next.to_string() << std::endl;
#endif
                //current_bsb = b->Hash(); // Found starting hash.
                break;
           }
           current_bsb = previous; // Walk backwards till the beginning...
        }
    }
#ifdef _DEBUG
        std::cout << "logos::bulk_pull_server::set_current_end: current_epoch: " << current_epoch.to_string() << " current_micro: " << current_micro.to_string() << " current_bsb: " << current_bsb.to_string() << " delegate_id: " << request->delegate_id << std::endl;
        std::cout << "logos::bulk_pull_server::set_current_end: e_end: " << request->e_end.to_string() << " m_end: " << request->m_end.to_string() << " b_end: " << request->b_end.to_string() << " delegate_id: " << request->delegate_id << std::endl;
#endif
}

void logos::bulk_pull_server::send_next ()
{
    BlockHash zero = 0;
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_server::send_next: " << std::endl;
#endif
	{
        BlockHash epoch_block = EpochBlock::getNextEpochBlock(connection->node->store, request->delegate_id, current_epoch);
        BlockHash micro_block = Micro::getNextMicroBlock(connection->node->store, request->delegate_id, current_micro);
        if(!current_epoch.is_zero()) { // && current_epoch != request->e_end) {
        //if(!current_epoch.is_zero() && current_epoch != request->e_end) {
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_server::send_next: epoch_block" << std::endl;
#endif
            std::shared_ptr<Epoch> e = EpochBlock::readEpochBlock(connection->node->store, current_epoch);
            BatchBlock::bulk_pull_response_epoch resp;
            resp.delegate_id = request->delegate_id; // m->delegateNumber;
#ifdef _DEBUG
            std::cout << "addr: " << (uint64_t)&resp.epoch << " src: " << (uint64_t)e.get() << " size: " << sizeof(Epoch) << std::endl;
#endif
            if(e == nullptr) {
                std::cout << " null return: " << current_epoch.to_string() << std::endl;
                current_epoch = zero;
                send_next();
                return;
            }
            memcpy(&resp.epoch,e.get(),sizeof(Epoch));
            if(current_epoch == request->e_end) {
                BlockHash zero = 0;
                current_epoch = zero; // We are at the end...
            } else {
                current_epoch = epoch_block;
            }

            auto send_buffer1(std::make_shared<std::vector<uint8_t>>(BatchBlock::bulk_pull_response_mesg_len, uint8_t(0)));
            {
                memcpy(send_buffer1->data(),(void *)&resp, sizeof(resp));
            }

            std::cout << " sending epoch: " << e->Hash().to_string() << std::endl;
		    auto this_l (shared_from_this ());
		    if (connection->node->config.logging.bulk_pull_logging ())
		    {
			    //BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % m->Hash ().to_string ());
		    }
            int size = (sizeof(BatchBlock::bulk_pull_response_epoch));
		    async_write (*connection->socket, boost::asio::buffer (send_buffer1->data (), size), [this_l,send_buffer1](boost::system::error_code const & ec, size_t size_a) {
			    this_l->sent_action (ec, size_a);
		    });
        } else if(!current_micro.is_zero()) { // && current_micro != request->m_end) {
        //} else if(!current_micro.is_zero() && current_micro != request->m_end) {
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_server::send_next: micro_block" << std::endl;
#endif
            std::shared_ptr<MicroBlock> m = Micro::readMicroBlock(connection->node->store, current_micro);
            BatchBlock::bulk_pull_response_micro resp;
            resp.delegate_id = request->delegate_id; // m->delegateNumber;
            if(m == nullptr) {
                current_micro = zero;
                send_next();
                return;
            }
            memcpy(&resp.micro,m.get(),sizeof(MicroBlock));
            if(current_micro == request->m_end) {
                BlockHash zero = 0;
                current_micro = micro_block;
            } else {
                current_micro = micro_block;
            }

            auto send_buffer1(std::make_shared<std::vector<uint8_t>>(BatchBlock::bulk_pull_response_mesg_len, uint8_t(0)));
            {
                memcpy(send_buffer1->data(),(void *)&resp, sizeof(resp));
            }

		    auto this_l (shared_from_this ());
		    if (connection->node->config.logging.bulk_pull_logging ())
		    {
			    BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % m->Hash ().to_string ());
		    }
            int size = (sizeof(BatchBlock::bulk_pull_response_micro));
		    async_write (*connection->socket, boost::asio::buffer (send_buffer1->data (), size), [this_l,send_buffer1](boost::system::error_code const & ec, size_t size_a) {
			    this_l->sent_action (ec, size_a);
		    });
        } else {
#ifdef _DEBUG
            std::cout << "logos::bulk_pull_server::send_next: bsb_block" << std::endl;
#endif
           // Get BSB blocks. 
           BlockHash bsb = BatchBlock::getNextBatchStateBlock(connection->node->store, request->delegate_id, current_bsb);
           static int total_count = 0;
           total_count++;
#if 0
#ifdef _DEBUG
           if(iter_count++ < 4)
#else
           if(!bsb.is_zero() && bsb != request->b_end)
#endif
#endif

           if(bsb.is_zero())
           {
                std::cout << "bsb is_zero" << std::endl;
           }
           else
           {
                std::cout << "bsb is_non_zero: current_bsb: " << current_bsb.to_string() <<  " delegate_id: " << request->delegate_id << std::endl;
           }

           if(!current_bsb.is_zero()) // && current_bsb != request->b_end)
           {
#ifdef _DEBUG
                std::cout << "logos::bulk_pull_server:: total_count: " << total_count << " delegate_id: " << request->delegate_id << std::endl; 
                std::cout << "logos::bulk_pull_server:: count: " << iter_count << " delegate_id: " << request->delegate_id << std::endl; 
#endif
                std::shared_ptr<BatchStateBlock> b = BatchBlock::readBatchStateBlock(connection->node->store, current_bsb);
                BatchBlock::bulk_pull_response resp;
                resp.delegate_id = request->delegate_id;
                if(b == nullptr) {
                    current_bsb = zero;
                    send_next();
                    return;
                }
                memcpy(&resp.block,b.get(),sizeof(BatchStateBlock));
#ifdef _DEBUG
                std::cout << " current_bsb: " << current_bsb.to_string() << " << b->Hash().to_string() " << b->Hash().to_string()  << " message_count: " << b->block_count << std::endl;
#endif
                std::cout << " RGDRGD is_non_zero: current_bsb: " << current_bsb.to_string() <<  " delegate_id: " << request->delegate_id << " request.b_end: " << request->b_end.to_string() << std::endl;
                if(current_bsb == request->b_end) {
                    BlockHash zero = 0;
                    current_bsb = zero; // We are at the end...
                } else {
                    current_bsb = bsb;
                }

                //auto send_buffer1(std::make_shared<std::vector<uint8_t>>(sizeof(resp), uint8_t(0)));
                auto send_buffer1(std::make_shared<std::vector<uint8_t>>(BatchBlock::bulk_pull_response_mesg_len, uint8_t(0)));
	            {
                    memcpy(send_buffer1->data(),(void *)&resp, sizeof(resp));
	            }

			    auto this_l (shared_from_this ());
			    if (connection->node->config.logging.bulk_pull_logging ())
			    {
				    BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % b->Hash ().to_string ());
			    }
                int size = (sizeof(BatchBlock::bulk_pull_response));
                //std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Half second throttle.
			    async_write (*connection->socket, boost::asio::buffer (send_buffer1->data (), size), [this_l,send_buffer1](boost::system::error_code const & ec, size_t size_a) {
				    this_l->sent_action (ec, size_a);
			    });
           } else {
#ifdef _DEBUG
                std::cout << "send_finished: current_bsb: " << current_bsb.to_string() << " request_end: " << request->b_end.to_string() << " delegate_id: " << request->delegate_id << std::endl;
#endif
		        send_finished ();
           }
        }
	}
}

std::unique_ptr<logos::block> logos::bulk_pull_server::get_next ()
{
	std::unique_ptr<logos::block> result;
	if (current != request->end)
	{
		logos::transaction transaction (connection->node->store.environment, nullptr, false);
		result = connection->node->store.block_get (transaction, current);
		if (result != nullptr)
		{
			auto previous (result->previous ());
			if (!previous.is_zero ())
			{
				current = previous;
			}
			else
			{
				current = request->end;
			}
		}
		else
		{
			current = request->end;
		}
	}
	return result;
}

void logos::bulk_pull_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_server::sent_action delegate_id:" << request->delegate_id << " size_a: " << size_a << std::endl;
#endif

	if (!ec)
	{
#ifdef _DEBUG
        std::cout << "logos::bulk_pull_server::sent_action:: send_next" << std::endl;
#endif
		send_next ();
	}
	else
	{
#ifdef _DEBUG
        std::cout << "logos::bulk_pull_server::sent_action:: error: message: " << ec.message() << std::endl;
#endif
        //send_next ();
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Unable to bulk send block: %1%") % ec.message ());
		}
        connection->socket->close();
		connection->finish_request ();
	}
}

void logos::bulk_pull_server::send_finished ()
{
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_server::send_finished delegate_id:" << request->delegate_id << std::endl;
#endif
	send_buffer.clear ();
	send_buffer.push_back (static_cast<uint8_t> (logos::block_type::not_a_block));
	auto this_l (shared_from_this ());
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (connection->node->log) << "Bulk sending finished";
	}
	async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->no_block_sent (ec, size_a);
	});
}

void logos::bulk_pull_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_server::no_block_sent: " << std::endl;
#endif
	if (!ec)
	{
		assert (size_a == 1);
#ifdef _DEBUG
        std::cout << "logos::bulk_pull_server::no_block_sent: finish_request: delegate_id: " << request->delegate_id << std::endl;
#endif
		connection->finish_request ();
	}
	else
	{
#ifdef _DEBUG
        std::cout << "logos::bulk_pull_server::no_block_sent: finish_request: error: delegate_id: " << request->delegate_id << " ec.message: " << ec.message() << std::endl;
#endif
		//connection->finish_request (); // MINE
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << "Unable to send not-a-block";
		}
        connection->socket->close();
		connection->finish_request ();
	}
}

logos::bulk_pull_server::bulk_pull_server (std::shared_ptr<logos::bootstrap_server> const & connection_a, std::unique_ptr<logos::bulk_pull> request_a) :
connection (connection_a),
request (std::move (request_a)),
iter_count(0)
{
#ifdef _DEBUG
    std::cout << "logos::bulk_pull_server::bulk_pull_server: delegate_id: " << request->delegate_id << std::endl;
#endif
	set_current_end ();
}

/**
 * Bulk pull of a range of blocks, or a checksum for a range of
 * blocks [min_hash, max_hash) up to a max of max_count.  mode
 * specifies whether the list is returned or a single checksum
 * of all the hashes.  The checksum is computed by XORing the
 * hash of all the blocks that would be returned
 */
void logos::bulk_pull_blocks_server::set_params ()
{
	assert (request != nullptr);

	if (connection->node->config.logging.bulk_pull_logging ())
	{
		std::string modeName = "<unknown>";

		switch (request->mode)
		{
			case logos::bulk_pull_blocks_mode::list_blocks: // Value is 0 which I think is set by default.
				modeName = "list";
				break;
			case logos::bulk_pull_blocks_mode::checksum_blocks:
				modeName = "checksum";
				break;
		}

		BOOST_LOG (connection->node->log) << boost::str (boost::format ("Bulk pull of block range starting, min (%1%) to max (%2%), max_count = %3%, mode = %4%") % request->min_hash.to_string () % request->max_hash.to_string () % request->max_count % modeName);
	}

	stream = connection->node->store.block_info_begin (stream_transaction, request->min_hash);

	if (request->max_hash < request->min_hash)
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Bulk pull of block range is invalid, min (%1%) is greater than max (%2%)") % request->min_hash.to_string () % request->max_hash.to_string ());
		}

		request->max_hash = request->min_hash;
	}
}

void logos::bulk_pull_blocks_server::send_next ()
{
	std::unique_ptr<logos::block> block (get_next ());
	if (block != nullptr)
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % block->hash ().to_string ());
		}

		send_buffer.clear ();
		auto this_l (shared_from_this ());

		if (request->mode == logos::bulk_pull_blocks_mode::list_blocks)
		{
			logos::vectorstream stream (send_buffer);
			logos::serialize_block (stream, *block);
		}
		else if (request->mode == logos::bulk_pull_blocks_mode::checksum_blocks)
		{
			checksum ^= block->hash ();
		}

		async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l](boost::system::error_code const & ec, size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}

	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Done sending blocks"));
		}

		if (request->mode == logos::bulk_pull_blocks_mode::checksum_blocks)
		{
			{
				send_buffer.clear ();
				logos::vectorstream stream (send_buffer);
				write (stream, static_cast<uint8_t> (logos::block_type::not_a_block));
				write (stream, checksum);
			}

			auto this_l (shared_from_this ());
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending checksum: %1%") % checksum.to_string ());
			}

			async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->send_finished ();
			});
		}
		else
		{
			send_finished ();
		}
	}
}

std::unique_ptr<logos::block> logos::bulk_pull_blocks_server::get_next ()
{
	std::unique_ptr<logos::block> result;
	bool out_of_bounds;

	out_of_bounds = false;
	if (request->max_count != 0)
	{
		if (sent_count >= request->max_count)
		{
			out_of_bounds = true;
		}

		sent_count++;
	}

	if (!out_of_bounds)
	{
		if (stream->first.size () != 0)
		{
			auto current = stream->first.uint256 ();
			if (current < request->max_hash)
			{
				logos::transaction transaction (connection->node->store.environment, nullptr, false);
				result = connection->node->store.block_get (transaction, current); // RGDSERVER Implement this and successor pointer should be made available.

				++stream;
			}
		}
	}
	return result;
}

void logos::bulk_pull_blocks_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		send_next ();
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Unable to bulk send block: %1%") % ec.message ());
		}
        connection->socket->close();
		connection->finish_request ();
	}
}

void logos::bulk_pull_blocks_server::send_finished ()
{
	send_buffer.clear ();
	send_buffer.push_back (static_cast<uint8_t> (logos::block_type::not_a_block));
	auto this_l (shared_from_this ());
	if (connection->node->config.logging.bulk_pull_logging ())
	{
		BOOST_LOG (connection->node->log) << "Bulk sending finished";
	}
	async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->no_block_sent (ec, size_a);
	});
}

void logos::bulk_pull_blocks_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		assert (size_a == 1);
		connection->finish_request ();
	}
	else
	{
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << "Unable to send not-a-block";
		}
        connection->socket->close();
		connection->finish_request ();
	}
}

logos::bulk_pull_blocks_server::bulk_pull_blocks_server (std::shared_ptr<logos::bootstrap_server> const & connection_a, std::unique_ptr<logos::bulk_pull_blocks> request_a) :
connection (connection_a),
request (std::move (request_a)),
stream (nullptr),
stream_transaction (connection_a->node->store.environment, nullptr, false),
sent_count (0),
checksum (0)
{
	set_params ();
}
