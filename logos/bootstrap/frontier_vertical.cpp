#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>
#include <logos/bootstrap/batch_block_frontier.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>

void logos::frontier_req_client::run () // RGD: This seems to do the actual work of making the frontier request. See receive_frontier which is called when we successfully write.
{ // RGD: Called from 'logos::bootstrap_attempt::request_frontier'
  // FRONTIER This is the start of it. We write the request as a request->serialize
  //          the request is age/count.
	std::unique_ptr<logos::frontier_req> request (new logos::frontier_req);
	request->start.clear ();
	request->age = std::numeric_limits<decltype (request->age)>::max (); // RGD: This looks to be the actual request, age and count max value of int.
	request->count = std::numeric_limits<decltype (request->age)>::max ();
    request->nr_delegate = NUMBER_DELEGATES; // TODO: Use same constant instead of hard-code.
    // RGDFRONT TODO 
    //          - This is the start of the request which is sent to the server, and the server responds.
    //          - So, have a loop here where we send for all n delegates and send over to the server.
    //            Then, write the server logic where we get the tips for the delegate and send.
    //            Alternatively, we can have one request and many responses in the server.
    //          - As we receive requests, we implement our algorithm using another method or by modifying
    //            the received_frontier algorithm, and in received_frontier, we decide whether to push or pull.
	auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		logos::vectorstream stream (*send_buffer);
		request->serialize (stream); // RGD: serialize request to be sent for a frontier request.
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_write (connection->socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()), [this_l, send_buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		if (!ec)
		{
			this_l->receive_frontier_header ();
		}
		else
		{
			if (this_l->connection->node->config.logging.network_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ());
			}
		}
	});
}

logos::frontier_req_client::frontier_req_client (std::shared_ptr<logos::bootstrap_client> connection_a) :
connection (connection_a),
current (0),
count (0),
bulk_push_cost (0)
{
	logos::transaction transaction (connection->node->store.environment, nullptr, false);
	next (transaction); // RGD: Initial call, sets up current
}

logos::frontier_req_client::~frontier_req_client ()
{
}

void logos::frontier_req_client::receive_frontier_header ()
{
	auto this_l (shared_from_this ());
	boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
            logos::block_type type (static_cast<logos::block_type> (this_l->connection->receive_buffer[0]));
            if(type == logos::block_type::frontier_block) {
			    boost::asio::async_read (this_l->connection->socket, boost::asio::buffer 
                    (this_l->connection->receive_buffer.data() + 1, sizeof(BatchBlock::frontier_response) - 1),
                    [this_l](boost::system::error_code const & ec, size_t size_a) {
                        this_l->received_batch_block_frontier(ec, size_a);
			    });
            } else {
			    this_l->receive_frontier();
            }
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type: %1%") % ec.message ());
			}
		}
	});
}

void logos::frontier_req_client::receive_frontier ()
{ // RGD: Called from frontier_req_client::run
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	size_t size_l (sizeof (logos::uint256_union) + sizeof (logos::uint256_union));
	boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), size_l), [this_l, size_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();

		// An issue with asio is that sometimes, instead of reporting a bad file descriptor during disconnect,
		// we simply get a size of 0.
		if (size_a == size_l)
		{
			this_l->received_frontier (ec, size_a); // RGD: If we succeeded, receive the frontier
		}
		else
		{
			if (this_l->connection->node->config.logging.network_message_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Invalid size: expected %1%, got %2%") % size_l % size_a);
			}
		}
	});
}

void logos::frontier_req_client::unsynced (MDB_txn * transaction_a, logos::block_hash const & head, logos::block_hash const & end)
{ // RGD: Called from 'logos::frontier_req_client::received_frontier'
  //      This is called when we are outofsync, in 'received_frontier'.
  // FRONTIER calls to bulk_push
	if (bulk_push_cost < bulk_push_cost_limit)
	{
        // RGDPUSH Setup our request. We may revisit when working on the frontier if we want a vector of request_info structs.
		connection->attempt->add_bulk_push_target (head, end); // RGD: Adds to 'bulk_push_targets', which where that queue is drained in 'logos::bulk_push_client::push'
		if (end.is_zero ())
		{
			bulk_push_cost += 2;
		}
		else
		{
			bulk_push_cost += 1;
		}
	}
}

// RGDFRONT This is the core algorithm. How do we request this, what does the server provide us, how
//          can we get server (peer) tips for each delegate and determine if we are ahead or behind
//          and then construct the query and do either push or pull ?
void logos::frontier_req_client::received_frontier (boost::system::error_code const & ec, size_t size_a)
{ // RGD: Called from 'logos::frontier_req_client::receive_frontier'
  // FRONTIER This looks to be the main method.
  //          unsynced does a push and we add_pull from here
  //          So this looks like the method to change along with helper methods.
  //          Review unsynced, next, request account in client in addition to
  //          how the request is made to start in run.
  //          For server, we are concerned with skip_old,send_next,next and subsequently,
  //          sent_action, send_finished, no_block_sent need review
  //
  // QN:
  // Should we request frontier (using head, timestamp, delegateid) then
  // Get sequence numbers/timestamps and then decide if they are in our db in which case
  // we skip, if not, we pull and add, should we also check our sequence numbers in the database
  // to determine if we are ahead, and if so, we push.
  //
  // Can we iterate our local db, and check ? Is that what current is here ?
  // Instead of account, we get a hash of the next block on the remote peer
  //
  // frontier_req_client::next is calling latest_begin from store which
  // is getting the next record from the db (current.number() + 1), and its getting set into current
  // current then is compared, and we decide what to do from there.
  // So its, 
  //    request frontier
  //    receive accounts
  //       using current and comparison to account received, decide
  //       to push or pull
  // This suggests that accounts are sequential. We may wish to use a sequence number on
  // batch blocks because of this. How do we construct a query in our database for
  // getting next based on either sequence number of timestamp ?
	if (!ec)
	{
		assert (size_a == sizeof (logos::uint256_union) + sizeof (logos::uint256_union));
		logos::account account;
		logos::bufferstream account_stream (connection->receive_buffer.data (), sizeof (logos::uint256_union));
		auto error1 (logos::read (account_stream, account)); // RGD: read is in blocks.cpp, gets sizeof(account) bytes from stream 'account_stream'.
		assert (!error1);
		logos::block_hash latest;
		logos::bufferstream latest_stream (connection->receive_buffer.data () + sizeof (logos::uint256_union), sizeof (logos::uint256_union));
		auto error2 (logos::read (latest_stream, latest)); // RGD: get account and get block hash 'latest'.
		assert (!error2);
		if (count == 0)
		{
			start_time = std::chrono::steady_clock::now ();
		}
		++count;
		std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>> (std::chrono::steady_clock::now () - start_time);
		double elapsed_sec = time_span.count ();
		double blocks_per_sec = (double)count / elapsed_sec;
		if (elapsed_sec > bootstrap_connection_warmup_time_sec && blocks_per_sec < bootstrap_minimum_frontier_blocks_per_sec)
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Aborting frontier req because it was too slow"));
			promise.set_value (true);
			return;
		}
		if (connection->attempt->should_log ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Received %1% frontiers from %2%") % std::to_string (count) % connection->socket.remote_endpoint ());
		}
		if (!account.is_zero ()) // RGD: Here we do the actual sync. IMPORTANT TODO re-read. FRONTIER
		{
			while (!current.is_zero () && current < account)
			{
				// We know about an account they don't.
				logos::transaction transaction (connection->node->store.environment, nullptr, true);
				unsynced (transaction, info.head, 0); // RGD: We know something they don't so we push it to them (write socket)
				next (transaction); // RGD: side-effects is that it updates current and info (account_info)
			}
			if (!current.is_zero ())
			{
				if (account == current)
				{
					logos::transaction transaction (connection->node->store.environment, nullptr, true);
					if (latest == info.head)
					{
						// In sync
					}
					else
					{
						if (connection->node->store.block_exists (transaction, latest))
						{
							// We know about a block they don't.
							unsynced (transaction, info.head, latest);
						}
						else
						{
                            // RGD: FRONTIER Decides what to add to pull queue
                            // connection->attempt->add_pull (logos::pull_info(start,end,delegate_id));
							connection->attempt->add_pull (logos::pull_info (account, latest, info.head));
							// Either we're behind or there's a fork we differ on
							// Either way, bulk pushing will probably not be effective
							// RGD: Adds to pulls queue
							bulk_push_cost += 5;
						}
					}
					next (transaction);
				}
				else
				{
					assert (account < current);
					connection->attempt->add_pull (logos::pull_info (account, latest, logos::block_hash (0)));
				}
			}
			else
			{
				connection->attempt->add_pull (logos::pull_info (account, latest, logos::block_hash (0)));
			}
			receive_frontier ();
		}
		else
		{
			{
				logos::transaction transaction (connection->node->store.environment, nullptr, true);
				while (!current.is_zero ())
				{
					// We know about an account they don't.
					unsynced (transaction, info.head, 0);
					next (transaction);
				}
			}
			if (connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (connection->node->log) << "Bulk push cost: " << bulk_push_cost;
			}
			{
				try
				{
					promise.set_value (false);
				}
				catch (std::future_error &)
				{
				}
				connection->attempt->pool_connection (connection);
			}
		}
	}
	else
	{
		if (connection->node->config.logging.network_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error while receiving frontier %1%") % ec.message ());
		}
	}
}

void logos::frontier_req_client::received_batch_block_frontier(boost::system::error_code const &ec, size_t size_a)
{
    if(!ec) {
        // TODO Here is where we change, if we get the entire image in one shot
        //      we can iterate
        uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
        std::shared_ptr<BatchBlock::frontier_response> frontier(new BatchBlock::frontier_response);
        memcpy(&(*frontier),data,sizeof(BatchBlock::frontier_response));
        // This is our frontier algorithm...
        // Get my tips...
        BlockHash epoch_tip = BatchBlock::getEpochBlockTip(connection->node->store, frontier->delegate_id);
        BlockHash micro_tip = BatchBlock::getMicroBlockTip(connection->node->store, frontier->delegate_id);
        BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(connection->node->store, frontier->delegate_id);
        // Get my seq numbers...
        uint32_t  epoch_seq = BatchBlock::getEpochBlockSeqNr(connection->node->store, frontier->delegate_id);
        uint32_t  micro_seq = BatchBlock::getMicroBlockSeqNr(connection->node->store, frontier->delegate_id);
        uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(connection->node->store, frontier->delegate_id);
        //  Am I behind or ahead for this delegate...
        if(epoch_seq <= frontier->epoch_block_seq_number && 
           micro_seq <= frontier->micro_block_seq_number && 
            bsb_seq < frontier->batch_block_seq_number) {
            // I have less sequence number than my peer, I am behind...
            // Construct a request to pull TODO
            connection->attempt->add_pull(logos::pull_info(0,0, // TODOVERTICAL [1] Send multiples of these from the server and process them here.
                         bsb_seq,frontier->batch_block_seq_number,
                         frontier->delegate_id,epoch_tip,frontier->epoch_block_tip,
                         micro_tip,frontier->micro_block_tip,
                         bsb_tip,frontier->batch_block_tip));
        } else if(epoch_seq >= frontier->epoch_block_seq_number &&
                  micro_seq >= frontier->micro_block_seq_number && 
                  bsb_seq > frontier->batch_block_seq_number) {
            // I have higher sequence number than my peer, I am ahead...
            // Construct a request to push TODO
            connection->attempt->add_bulk_push_target(logos::request_info(0,0, // TODOVERTICAL Change this to send multiples (need store of current block and target.
                                                                               //              target is always our tips. Current should be stored somewhere ? Initially null, when we request, 
                                                                               //              then subsequently, the current one sent (we should store initial response and iterate by getting next).
                                                                               //              And then do multiple pushes
                         frontier->batch_block_seq_number,bsb_seq,
                         frontier->delegate_id,frontier->epoch_block_tip,epoch_tip,
                         frontier->micro_block_tip,micro_tip,
                         frontier->batch_block_tip,bsb_tip));
        } else if(epoch_seq == frontier->epoch_block_seq_number &&
                  micro_seq == frontier->micro_block_seq_number &&
                  bsb_seq == frontier->batch_block_seq_number) {
                // We are in sync, continue processing...
        } else {
		    if (connection->node->config.logging.bulk_pull_logging ())
		    {
			    BOOST_LOG (connection->node->log) << "invalid frontier state";
		    }
        }
    }
}

// RGDFRONT See where this is called and how the current/info is set. We want to keep
//          there logic, and add ours ...
void logos::frontier_req_client::next (MDB_txn * transaction_a)
{
	auto iterator (connection->node->store.latest_begin (transaction_a, logos::uint256_union (current.number () + 1)));
	if (iterator != connection->node->store.latest_end ())
	{
		current = logos::account (iterator->first.uint256 ());
		info = logos::account_info (iterator->second); // RGD: Set info
	}
	else
	{
		current.clear ();
	}
}

// RGDFRONT Start with the server.
logos::frontier_req_server::frontier_req_server (std::shared_ptr<logos::bootstrap_server> const & connection_a, std::unique_ptr<logos::frontier_req> request_a) :
connection (connection_a),
current (request_a->start.number () - 1),
info (0, 0, 0, 0, 0, 0),
request (std::move (request_a))
{
	next ();
	skip_old ();
    send_batch_blocks_frontier();
}

void logos::frontier_req_server::skip_old ()
{
	if (request->age != std::numeric_limits<decltype (request->age)>::max ())
	{
		auto now (logos::seconds_since_epoch ());
		while (!current.is_zero () && (now - info.modified) >= request->age)
		{
			next ();
		}
	}
}

void logos::frontier_req_server::send_next ()
{
	if (!current.is_zero ())
	{
		{
			send_buffer.clear ();
			logos::vectorstream stream (send_buffer);
			write (stream, current.bytes);
			write (stream, info.head.bytes);
		}
		auto this_l (shared_from_this ());
		if (connection->node->config.logging.bulk_pull_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending frontier for %1% %2%") % current.to_account () % info.head.to_string ());
		}
		next ();
		async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l](boost::system::error_code const & ec, size_t size_a) {
			this_l->sent_action (ec, size_a);
		});
	}
	else
	{
		send_finished ();
	}
}

void logos::frontier_req_server::send_finished ()
{
	{
		send_buffer.clear ();
		logos::vectorstream stream (send_buffer);
		logos::uint256_union zero (0);
		write (stream, zero.bytes);
		write (stream, zero.bytes);
	}
	auto this_l (shared_from_this ());
	if (connection->node->config.logging.network_logging ())
	{
		BOOST_LOG (connection->node->log) << "Frontier sending finished";
	}
	async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->no_block_sent (ec, size_a);
	});
}

void logos::frontier_req_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		connection->finish_request ();
	}
	else
	{
		if (connection->node->config.logging.network_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error sending frontier finish: %1%") % ec.message ());
		}
	}
}

void logos::frontier_req_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		send_next ();
	}
	else
	{
		if (connection->node->config.logging.network_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error sending frontier pair: %1%") % ec.message ());
		}
	}
}

void logos::frontier_req_server::next ()
{
	logos::transaction transaction (connection->node->store.environment, nullptr, false);
	auto iterator (connection->node->store.latest_begin (transaction, current.number () + 1));
	if (iterator != connection->node->store.latest_end ())
	{
		current = logos::uint256_union (iterator->first.uint256 ());
		info = logos::account_info (iterator->second); // RGD: Looks like where it is setting up info member variable.
	}
	else
	{
		current.clear ();
	}
}

// TODOVERTICAL Change this to send multiples (need store of current block and target.
//              target is always our tips. Current should be stored somewhere ? Initially null, when we request, 
//              then subsequently, the current one sent (we should store initial response and iterate by getting next).
//              And then do multiple sends of the resp. Then, in the above 'TODOVERTICAL [1]' we should process
//              each pull response seperately. So we have, for each delegate, if current not at end, then get tip, send
//
void logos::frontier_req_server::send_batch_blocks_frontier()
{
    // TODO Construct single response with all delegates states
	auto this_l (shared_from_this ());
    if(request->nr_delegate == NUMBER_DELEGATES) {
        // Get the frontier for all delegates and send them one by one for processing...
        for(int i = 0; i < request->nr_delegate; ++i) {
            BatchBlock::frontier_response resp;
            // Get my tips...
            BlockHash epoch_tip = BatchBlock::getEpochBlockTip(connection->node->store, i);
            BlockHash micro_tip = BatchBlock::getMicroBlockTip(connection->node->store, i);
            BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(connection->node->store, i);
            // Get my seq numbers...
            uint32_t  epoch_seq = BatchBlock::getEpochBlockSeqNr(connection->node->store, i);
            uint32_t  micro_seq = BatchBlock::getMicroBlockSeqNr(connection->node->store, i);
            uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(connection->node->store, i);
            // Fill in the response...
            resp.timestamp_start        = 0;
            resp.timestamp_end          = 0;
            resp.delegate_id            = i;
            resp.epoch_block_tip        = epoch_tip;
            resp.micro_block_tip        = micro_tip;
            resp.batch_block_tip        = bsb_tip;
            resp.epoch_block_seq_number = epoch_seq;
            resp.micro_block_seq_number = micro_seq;
            resp.batch_block_seq_number = bsb_seq;
            // All done, write it out to the client...
            {
                send_buffer.clear();
                logos::vectorstream stream(send_buffer);
                write(stream, (void *)&resp, sizeof(resp)); // See ../lib/blocks.hpp for write implementation.
            }
	        async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l](boost::system::error_code const & ec, size_t size_a) {
		            this_l->no_block_sent (ec, size_a);
    	    });
        }
    } else {
        // Log an error.
		if (this_l->connection->node->config.logging.bulk_pull_logging ())
		{
		    BOOST_LOG (this_l->connection->node->log) << "number of delegates does not match: server: " << NUMBER_DELEGATES << " client: " << request->nr_delegate;
		}
    }
}
