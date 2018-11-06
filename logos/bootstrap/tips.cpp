#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>
#include <logos/bootstrap/batch_block_tips.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/epoch.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>

extern std::atomic<int> total_pulls;

void logos::tips_req_client::run ()
{ // RGD: Called from 'logos::bootstrap_attempt::request_tips'
  // tips This is the start of it. We write the request as a request->serialize
  // and kick off the process...
#ifdef _DEBUG
    std::cout << "tips_req_client::run" << std::endl;
#endif

    // TODO We may want more fine grain flag that we are not
    //      done with prior requests, but this is the method
    //      already supported.
    //if(connection->attempt->still_pulling()) {
    {
    //std::lock_guard<std::mutex> mutex (connection->attempt->mutex);
#if 0
    while(total_pulls > 1) {
        std::cout << "logos::tips_req_client:: total_pulls: " << total_pulls << std::endl;
        sleep(1);
    }
#endif
    if(connection->attempt->pulling > 0 || total_pulls > 0) {
        try {
            promise.set_value(false);
        } catch(...) {}
#ifdef _DEBUG
        std::cout << "logos::tips_req_client:: total_pulls: " << total_pulls << std::endl;
        std::cout << "logos::tips_req_client::run: still pending" << std::endl;
#endif
        //connection->attempt->pool_connection(connection);
        return;
    }
    }
#ifdef _DEBUG
    std::cout << "logos::tips_req_client:: total_pulls: " << total_pulls << std::endl;
#endif
	std::unique_ptr<logos::frontier_req> request (new logos::frontier_req);
	request->start.clear ();
	request->age = std::numeric_limits<decltype (request->age)>::max ();
	request->count = std::numeric_limits<decltype (request->age)>::max ();
    request->nr_delegate = NUMBER_DELEGATES; // TODO: Use same constant instead of hard-code.

#ifdef _DEBUG
    std::cout << "::run count: " << request->count << " age: " << request->age << " nr_delegate: " << request->nr_delegate << " NUMBER_DELEGATES " << NUMBER_DELEGATES << std::endl;
#endif

	auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		logos::vectorstream stream (*send_buffer);
		request->serialize (stream);
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_write (connection->socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()), [this_l, send_buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		if (!ec)
		{
#ifdef _DEBUG
            std::cout << "this_l->receive_tips_header:" << std::endl;
#endif
			this_l->receive_tips_header ();
		}
		else
		{
			if (this_l->connection->node->config.logging.network_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ());
			}
            try {
                std::cout << "tips:: line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
                this_l->promise.set_value(true);
            } catch(...) {}
		}
	});
#ifdef _PROMISE
    promise.set_value(false);
#endif
}

logos::tips_req_client::tips_req_client (std::shared_ptr<logos::bootstrap_client> connection_a) :
connection (connection_a),
current (0),
count (0),
bulk_push_cost (0)
{
	logos::transaction transaction (connection->node->store.environment, nullptr, false);
	next (transaction);
}

logos::tips_req_client::~tips_req_client ()
{
#ifdef _DEBUG
    std::cout << "logos::tips_req_client::~tips_req_client" << std::endl;
#endif
}

void logos::tips_req_client::receive_tips_header ()
{
	auto this_l (shared_from_this ());
	boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
            logos::block_type type (static_cast<logos::block_type> (this_l->connection->receive_buffer[0]));
            if(type == logos::block_type::frontier_block) {
#ifdef _DEBUG
                std::cout << "received_batch_block_tips" << std::endl;
#endif
			    boost::asio::async_read (this_l->connection->socket, boost::asio::buffer 
                    (this_l->connection->receive_buffer.data() + 1, sizeof(BatchBlock::tips_response) - 1),
                    [this_l](boost::system::error_code const & ec, size_t size_a) {
                        this_l->received_batch_block_tips(ec, size_a);
			    });
            } else {
#ifdef _DEBUG
                std::cout << "received_tips" << std::endl;
#endif
			    this_l->receive_tips();
            }
		}
		else
		{
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type: %1%") % ec.message ());
			}
            try {
                std::cout << "tips:: line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
                this_l->promise.set_value(true);
            } catch(...) {}
		}
	});
}

void logos::tips_req_client::receive_tips ()
{ // RGD: Called from tips_req_client::run
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	size_t size_l (sizeof (logos::uint256_union) + sizeof (logos::uint256_union));
	boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), size_l), [this_l, size_l](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();

		// An issue with asio is that sometimes, instead of reporting a bad file descriptor during disconnect,
		// we simply get a size of 0.
		if (size_a == size_l)
		{
			this_l->received_tips (ec, size_a);
		}
		else
		{
			if (this_l->connection->node->config.logging.network_message_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Invalid size: expected %1%, got %2%") % size_l % size_a);
			}
            try {
                std::cout << "tips:: line: " << __LINE__ << std::endl;
                this_l->promise.set_value(true);
            } catch(...) {}
		}
	});
}

void logos::tips_req_client::unsynced (MDB_txn * transaction_a, logos::block_hash const & head, logos::block_hash const & end)
{ // RGD: Called from 'logos::tips_req_client::received_tips'
}

void logos::tips_req_client::received_tips (boost::system::error_code const & ec, size_t size_a)
{ //RGD: Called from 'logos::tips_req_client::receive_tips'
}

void logos::tips_req_client::received_batch_block_tips(boost::system::error_code const &ec, size_t size_a)
{
#ifdef _DEBUG
    static int count = 0;
    if(count++ > 100000) {
        try {
            std::cout << "logos::tips_req_client::received_batch_block_tips exceeded limit" << std::endl;
            promise.set_value(false);
        } catch(...) {}
        connection->attempt->pool_connection(connection);
        return;
    }
    std::cout << "logos::tips_req_client::received_batch_block_tips::count: " << count << std::endl;
#endif

#ifdef _DEBUG
    std::cout << "logos::tips_req_client::received_batch_block_tips: ec: " << ec << std::endl;
#endif

    if(!ec) {
        uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
        std::shared_ptr<BatchBlock::tips_response> tips(new BatchBlock::tips_response);
        memcpy(&(*tips),data,sizeof(BatchBlock::tips_response));

#ifdef _DEBUG
        std::cout << "logos::tips_req_client::received_batch_block_tips : " << *tips << std::endl;
#endif
        // This is our tips algorithm...
        // Get my tips...
        BlockHash epoch_tip = EpochBlock::getEpochBlockTip(connection->node->store, tips->delegate_id);
        BlockHash micro_tip = Micro::getMicroBlockTip(connection->node->store, tips->delegate_id);
        BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(connection->node->store, tips->delegate_id);
        // Get my seq numbers...
        uint32_t  epoch_seq = EpochBlock::getEpochBlockSeqNr(connection->node->store, tips->delegate_id);
        uint32_t  micro_seq = Micro::getMicroBlockSeqNr(connection->node->store, tips->delegate_id);
        uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(connection->node->store, tips->delegate_id);
        //  Am I behind or ahead for this delegate...
#ifdef _DEBUG
        std::cout << "receiving tips..." << std::endl;
#endif

        BlockHash empty;

        // Send out our request for epoch and micro blocks.
        // We do this only once per request for tips.
        // The underlying pull/push code handles one of three different
        // block types (epoch/micro/bsb) and sends them to validator
        // when blocks arrive.
        if(tips->delegate_id == 0) {
            // Get Epoch blocks...
	        if(epoch_seq < tips->epoch_block_seq_number) {
	            // I have less sequence number than my peer, I am behind...
	            connection->attempt->add_pull(logos::pull_info(0,0,
	                         0,0,
	                         tips->delegate_id,epoch_tip,tips->epoch_block_tip,
	                         empty,empty,
	                         empty,empty));
	#ifdef _DEBUG
	            std::cout << "logos::tips_req_client::received_batch_block_tips:: bulk_pull: delegate_id: " << tips->delegate_id << std::endl;
	#endif
	        } else if(epoch_seq > tips->epoch_block_seq_number) {
	            // I have higher sequence number than my peer, I am ahead...
	            // Construct a request to push TODO
	            connection->attempt->add_bulk_push_target(logos::request_info(0,0,
	                         0,0,
	                         tips->delegate_id,tips->epoch_block_tip,epoch_tip,
	                         empty,empty,
	                         empty,empty));
	#ifdef _DEBUG
	            std::cout << "logos::tips_req_client::received_batch_block_tips:: bulk_push: delegate_id: " << tips->delegate_id << std::endl; 
	#endif
	        } else if(epoch_seq == tips->epoch_block_seq_number) {
	                // We are in sync, continue processing...
	#ifdef _DEBUG
	                std::cout << "in sync" << std::endl;
	#endif
	        } else {
	#ifdef _DEBUG
	            std::cout << "logos::tips_req_client::received_batch_block_tips:: error..." << std::endl;
	#endif
			    if (connection->node->config.logging.bulk_pull_logging ())
			    {
				    BOOST_LOG (connection->node->log) << "invalid tips state";
			    }
	        }

            // Get micro blocks...
	        if(micro_seq < tips->micro_block_seq_number) {
	            // I have less sequence number than my peer, I am behind...
	            connection->attempt->add_pull(logos::pull_info(0,0,
	                         0,0,
	                         tips->delegate_id,
                             empty,empty,
                             micro_tip,tips->micro_block_tip,
	                         empty,empty));
	#ifdef _DEBUG
	            std::cout << "logos::tips_req_client::received_batch_block_tips:: bulk_pull: delegate_id: " << tips->delegate_id << std::endl;
	#endif
	        } else if(micro_seq > tips->micro_block_seq_number) {
	            // I have higher sequence number than my peer, I am ahead...
	            // Construct a request to push TODO
	            connection->attempt->add_bulk_push_target(logos::request_info(0,0,
	                         0,0,
	                         tips->delegate_id,
	                         empty,empty,
                             tips->micro_block_tip,micro_tip,
	                         empty,empty));
	#ifdef _DEBUG
	            std::cout << "logos::tips_req_client::received_batch_block_tips:: bulk_push: delegate_id: " << tips->delegate_id << std::endl; 
	#endif
	        } else if(epoch_seq == tips->epoch_block_seq_number) {
	                // We are in sync, continue processing...
	#ifdef _DEBUG
	                std::cout << "in sync" << std::endl;
	#endif
	        } else {
	#ifdef _DEBUG
	            std::cout << "logos::tips_req_client::received_batch_block_tips:: error..." << std::endl;
	#endif
			    if (connection->node->config.logging.bulk_pull_logging ())
			    {
				    BOOST_LOG (connection->node->log) << "invalid tips state";
			    }
	        }
	    
        }

        if(epoch_seq <= tips->epoch_block_seq_number && 
           micro_seq <= tips->micro_block_seq_number && 
            bsb_seq < tips->batch_block_seq_number) {
            // I have less sequence number than my peer, I am behind...
            connection->attempt->add_pull(logos::pull_info(0,0,
                         bsb_seq,tips->batch_block_seq_number,
                         tips->delegate_id,
                         empty,empty,
                         empty,empty,
                         bsb_tip,tips->batch_block_tip));
#ifdef _DEBUG
            std::cout << "logos::tips_req_client::received_batch_block_tips:: bulk_pull: delegate_id: " << tips->delegate_id << std::endl;
#endif
        } else if(epoch_seq >= tips->epoch_block_seq_number &&
                  micro_seq >= tips->micro_block_seq_number && 
                  bsb_seq > tips->batch_block_seq_number) {
            // I have higher sequence number than my peer, I am ahead...
            connection->attempt->add_bulk_push_target(logos::request_info(0,0,
                         tips->batch_block_seq_number,bsb_seq,
                         tips->delegate_id,
                         empty,empty,
                         empty,empty,
                         tips->batch_block_tip,bsb_tip));
#ifdef _DEBUG
            std::cout << "logos::tips_req_client::received_batch_block_tips:: bulk_push: delegate_id: " << tips->delegate_id << std::endl; 
#endif
        } else if(epoch_seq == tips->epoch_block_seq_number &&
                  micro_seq == tips->micro_block_seq_number &&
                  bsb_seq == tips->batch_block_seq_number) {
                // We are in sync, continue processing...
#ifdef _DEBUG
                std::cout << "in sync" << std::endl;
#endif
        } else {
#ifdef _DEBUG
            std::cout << "logos::tips_req_client::received_batch_block_tips:: error..." << std::endl;
#endif
		    if (connection->node->config.logging.bulk_pull_logging ())
		    {
			    BOOST_LOG (connection->node->log) << "invalid tips state";
		    }
        }
        if(tips->delegate_id == (NUMBER_DELEGATES-1)) {
            try {
                promise.set_value(false);
            } catch(...) {}
            connection->attempt->pool_connection (connection);
        }
		receive_tips_header ();
    } else {
#ifdef _DEBUG
        std::cout << "logos::tips_req_client::received_batch_block_tips error..." << std::endl;
#endif
    }
}

void logos::tips_req_client::next (MDB_txn * transaction_a)
{
}

// RGD Server sends tips to the client, client decides what to do (i.e., to push or pull)
logos::tips_req_server::tips_req_server (std::shared_ptr<logos::bootstrap_server> const & connection_a, std::unique_ptr<logos::frontier_req> request_a) :
connection (connection_a),
current (request_a->start.number () - 1),
info (0, 0, 0, 0, 0, 0, 0),
next_delegate(0),
request (std::move (request_a))
{
    nr_delegate = request->nr_delegate;
#ifdef _DEBUG
    std::cout << "logos::tips_req_server::tips_req_server request->nr_delegate: " << request->nr_delegate << " nr_delegate:: " << nr_delegate << " NUMBER_DELEGATES " << NUMBER_DELEGATES << std::endl;
#endif
}

logos::tips_req_server::~tips_req_server()
{
#ifdef _DEBUG
    std::cout << "logos::~tips_req_server:: called" << std::endl;
#endif
}

void logos::tips_req_server::skip_old ()
{
}

void logos::tips_req_server::send_next ()
{
    std::cout << "logos::tips_req_server::send_next: " << std::endl;

    if(nr_delegate > next_delegate)
    {
#ifdef _DEBUG
       std::cout << "logos::tips_req_server::send_next:: next_delegate: " << next_delegate << " nr_delegate: " << nr_delegate << std::endl;
#endif
       send_batch_blocks_tips(); // RGD Hack
       next_delegate++; 
	}
	else
	{
#ifdef _DEBUG
        std::cout << "logos::tips_req_server::send_next:: send_finished" << std::endl;
#endif
		send_finished ();
	}
}

void logos::tips_req_server::send_finished ()
{
#ifdef _DEBUG
    std::cout << "logos::tips_req_server::send_finished" << std::endl;
#endif

    auto send_buffer (std::make_shared<std::vector<uint8_t>>(sizeof(BatchBlock::tips_response), uint8_t(0)));
	//auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		//logos::vectorstream stream (*send_buffer);
        BatchBlock::tips_response resp;
        memset(&resp,0x0,sizeof(resp));
        resp.delegate_id            = -1;
        memcpy(send_buffer->data(),(void *)&resp, sizeof(resp));
        //write(stream,(void *)&resp, sizeof(resp)); // See ../lib/blocks.hpp for write implementation.
	}

    auto this_l = this;
	if (connection->node->config.logging.network_logging ())
	{
		BOOST_LOG (connection->node->log) << "tips sending finished";
	}

	async_write (*connection->socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()), [this_l,send_buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->no_block_sent (ec, size_a);
	});
}

void logos::tips_req_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
#ifdef _DEBUG
        std::cout << " logos::tips_req_server::no_block_sent connection: " << connection << std::endl;
#endif
		connection->finish_request ();
	}
	else
	{
		if (connection->node->config.logging.network_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error sending tips finish: %1%") % ec.message ());
		}
	}
}

void logos::tips_req_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		send_next ();
	}
	else
	{
		if (connection->node->config.logging.network_logging ())
		{
			BOOST_LOG (connection->node->log) << boost::str (boost::format ("Error sending tips pair: %1%") % ec.message ());
		}
	}
}

void logos::tips_req_server::next ()
{
}

void logos::tips_req_server::send_batch_blocks_tips()
{
#ifdef _DEBUG
    std::cout << "logos::tips_req_server::send_batch_blocks_tips: " << std::endl;
#endif

	auto this_l = this;

    if(nr_delegate == NUMBER_DELEGATES) {
        // RGD Get the tips for all delegates and send them one by one for processing...
        for(int i = 0; i < nr_delegate; i++)
        {
            BatchBlock::tips_response resp;
            // Get my tips...
            BlockHash epoch_tip = EpochBlock::getEpochBlockTip(connection->node->store, i);
            BlockHash micro_tip = Micro::getMicroBlockTip(connection->node->store, i);
            BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(connection->node->store, i);
            // Get my seq numbers...
            uint32_t  epoch_seq = EpochBlock::getEpochBlockSeqNr(connection->node->store, i);
            uint32_t  micro_seq = Micro::getMicroBlockSeqNr(connection->node->store, i);
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
            auto send_buffer1 (std::make_shared<std::vector<uint8_t>>(sizeof(BatchBlock::tips_response), uint8_t(0)));
	        //auto send_buffer1 (std::make_shared<std::vector<uint8_t>> ());
            {
                //logos::vectorstream stream(*send_buffer1);
                //write(stream, (void *)&resp, sizeof(resp)); // See ../lib/blocks.hpp for write implementation.
                memcpy(send_buffer1->data(),(void *)&resp, sizeof(resp));
            }

#ifdef _DEBUG
            std::cout << "send_batch_blocks_tips this: " << this << " connection: " << connection << " this_l->connection: " << this_l->connection << std::endl;
#endif
            boost::asio::write(*connection->socket, boost::asio::buffer (send_buffer1->data (), send_buffer1->size ()),
                    boost::asio::transfer_all());

        }
    } else {
#ifdef _DEBUG
        std::cout << "logos::tips_req_server::send_batch_blocks_tips error: " << std::endl
                  << " nr_delegate: " << nr_delegate << " NUMBER_DELEGATES " << NUMBER_DELEGATES << std::endl;
#endif
        // Log an error.
		if (this_l->connection->node->config.logging.bulk_pull_logging ())
		{
		    BOOST_LOG (this_l->connection->node->log) << "number of delegates does not match: server: " << NUMBER_DELEGATES << " client: " << nr_delegate;
		}
    }
}
