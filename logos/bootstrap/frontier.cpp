#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>
#include <logos/bootstrap/batch_block_frontier.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>

#define _DEBUG 1

void logos::frontier_req_client::run ()
{ // RGD: Called from 'logos::bootstrap_attempt::request_frontier'
  // FRONTIER This is the start of it. We write the request as a request->serialize
  // and kick off the process...
#ifdef _DEBUG
    std::cout << "frontier_req_client::run" << std::endl;
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
    promise.set_value(false);
}

logos::frontier_req_client::frontier_req_client (std::shared_ptr<logos::bootstrap_client> connection_a) :
connection (connection_a),
current (0),
count (0),
bulk_push_cost (0)
{
	logos::transaction transaction (connection->node->store.environment, nullptr, false);
	next (transaction);
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
#ifdef _DEBUG
                std::cout << "received_batch_block_frontier" << std::endl;
#endif
			    boost::asio::async_read (this_l->connection->socket, boost::asio::buffer 
                    (this_l->connection->receive_buffer.data() + 1, sizeof(BatchBlock::frontier_response) - 1),
                    [this_l](boost::system::error_code const & ec, size_t size_a) {
                        this_l->received_batch_block_frontier(ec, size_a);
			    });
            } else {
#ifdef _DEBUG
                std::cout << "received_frontier" << std::endl;
#endif
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
			this_l->received_frontier (ec, size_a);
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
}

void logos::frontier_req_client::received_frontier (boost::system::error_code const & ec, size_t size_a)
{ //RGD: Called from 'logos::frontier_req_client::receive_frontier'
}

void logos::frontier_req_client::received_batch_block_frontier(boost::system::error_code const &ec, size_t size_a)
{
#ifdef _DEBUG
    static int count = 0;
    if(count++ > 32) {
        return;
    }
#endif

#ifdef _DEBUG
    std::cout << "logos::frontier_req_client::received_batch_block_frontier: ec: " << ec << std::endl;
#endif

    if(!ec) {
        uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
        std::shared_ptr<BatchBlock::frontier_response> frontier(new BatchBlock::frontier_response);
        memcpy(&(*frontier),data,sizeof(BatchBlock::frontier_response));

#ifdef _DEBUG
        std::cout << "logos::frontier_req_client::received_batch_block_frontier : " << *frontier << std::endl;
#endif
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
#ifdef _DEBUG
        std::cout << "receiving frontiers..." << std::endl;
#endif
        if(epoch_seq <= frontier->epoch_block_seq_number && 
           micro_seq <= frontier->micro_block_seq_number && 
            bsb_seq < frontier->batch_block_seq_number) {
            // I have less sequence number than my peer, I am behind...
            connection->attempt->add_pull(logos::pull_info(0,0,
                         bsb_seq,frontier->batch_block_seq_number,
                         frontier->delegate_id,epoch_tip,frontier->epoch_block_tip,
                         micro_tip,frontier->micro_block_tip,
                         bsb_tip,frontier->batch_block_tip));
#ifdef _DEBUG
            std::cout << "logos::frontier_req_client::received_batch_block_frontier:: bulk_pull: delegate_id: " << frontier->delegate_id << std::endl;
#endif
        } else if(epoch_seq >= frontier->epoch_block_seq_number &&
                  micro_seq >= frontier->micro_block_seq_number && 
                  bsb_seq > frontier->batch_block_seq_number) {
            // I have higher sequence number than my peer, I am ahead...
            // Construct a request to push TODO
            connection->attempt->add_bulk_push_target(logos::request_info(0,0,
                         frontier->batch_block_seq_number,bsb_seq,
                         frontier->delegate_id,frontier->epoch_block_tip,epoch_tip,
                         frontier->micro_block_tip,micro_tip,
                         frontier->batch_block_tip,bsb_tip));
#ifdef _DEBUG
            std::cout << "logos::frontier_req_client::received_batch_block_frontier:: bulk_push: delegate_id: " << frontier->delegate_id << std::endl; 
#endif
        } else if(epoch_seq == frontier->epoch_block_seq_number &&
                  micro_seq == frontier->micro_block_seq_number &&
                  bsb_seq == frontier->batch_block_seq_number) {
                // We are in sync, continue processing...
#ifdef _DEBUG
                std::cout << "in sync" << std::endl;
#endif
        } else {
#ifdef _DEBUG
            std::cout << "logos::frontier_req_client::received_batch_block_frontier:: error..." << std::endl;
#endif
		    if (connection->node->config.logging.bulk_pull_logging ())
		    {
			    BOOST_LOG (connection->node->log) << "invalid frontier state";
		    }
        }
		receive_frontier_header ();
    } else {
#ifdef _DEBUG
        std::cout << "logos::frontier_req_client::received_batch_block_frontier error..." << std::endl;
#endif
    }
}

void logos::frontier_req_client::next (MDB_txn * transaction_a)
{
}

// RGD Server sends tips to the client, client decides what to do (i.e., to push or pull)
logos::frontier_req_server::frontier_req_server (std::shared_ptr<logos::bootstrap_server> const & connection_a, std::unique_ptr<logos::frontier_req> request_a) :
connection (connection_a),
current (request_a->start.number () - 1),
info (0, 0, 0, 0, 0, 0, 0),
next_delegate(0),
request (std::move (request_a))
{
    nr_delegate = request->nr_delegate;
#ifdef _DEBUG
    std::cout << "logos::frontier_req_server::frontier_req_server request->nr_delegate: " << request->nr_delegate << " nr_delegate:: " << nr_delegate << " NUMBER_DELEGATES " << NUMBER_DELEGATES << std::endl;
#endif
}

void logos::frontier_req_server::skip_old ()
{
}

void logos::frontier_req_server::send_next ()
{
    std::cout << "logos::frontier_req_server::send_next: " << std::endl;

    if(nr_delegate > next_delegate)
    {
#ifdef _DEBUG
       std::cout << "logos::frontier_req_server::send_next:: next_delegate: " << next_delegate << " nr_delegate: " << nr_delegate << std::endl;
#endif
       send_batch_blocks_frontier(); // RGD Hack
       next_delegate++; 
	}
	else
	{
#ifdef _DEBUG
        std::cout << "logos::frontier_req_server::send_next:: send_finished" << std::endl;
#endif
		send_finished ();
	}
}

void logos::frontier_req_server::send_finished ()
{
#ifdef _DEBUG
    std::cout << "logos::frontier_req_server::send_finished" << std::endl;
#endif

    auto send_buffer (std::make_shared<std::vector<uint8_t>>(sizeof(BatchBlock::frontier_response), uint8_t(0)));
	//auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		//logos::vectorstream stream (*send_buffer);
        BatchBlock::frontier_response resp;
        memset(&resp,0x0,sizeof(resp));
        resp.delegate_id            = -1;
        memcpy(send_buffer->data(),(void *)&resp, sizeof(resp));
        //write(stream,(void *)&resp, sizeof(resp)); // See ../lib/blocks.hpp for write implementation.
	}

    auto this_l = this;
	if (connection->node->config.logging.network_logging ())
	{
		BOOST_LOG (connection->node->log) << "Frontier sending finished";
	}

	async_write (*connection->socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()), [this_l,send_buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->no_block_sent (ec, size_a);
	});
}

void logos::frontier_req_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
        std::cout << " logos::frontier_req_server::no_block_sent connection: " << connection << std::endl;
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
}

void logos::frontier_req_server::send_batch_blocks_frontier()
{
#ifdef _DEBUG
    std::cout << "logos::frontier_req_server::send_batch_blocks_frontier: " << std::endl;
#endif

	auto this_l = this;

    if(nr_delegate == NUMBER_DELEGATES) {
        // RGD Get the frontier for all delegates and send them one by one for processing...
        for(int i = 0; i < nr_delegate; i++)
        {
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
            auto send_buffer1 (std::make_shared<std::vector<uint8_t>>(sizeof(BatchBlock::frontier_response), uint8_t(0)));
	        //auto send_buffer1 (std::make_shared<std::vector<uint8_t>> ());
            {
                //logos::vectorstream stream(*send_buffer1);
                //write(stream, (void *)&resp, sizeof(resp)); // See ../lib/blocks.hpp for write implementation.
                memcpy(send_buffer1->data(),(void *)&resp, sizeof(resp));
            }

#ifdef _DEBUG
            std::cout << "send_batch_blocks_frontier this: " << this << " connection: " << connection << " this_l->connection: " << this_l->connection << std::endl;
#endif
            boost::asio::write(*connection->socket, boost::asio::buffer (send_buffer1->data (), send_buffer1->size ()),
                    boost::asio::transfer_all());

        }
    } else {
#ifdef _DEBUG
        std::cout << "logos::frontier_req_server::send_batch_blocks_frontier error: " << std::endl
                  << " nr_delegate: " << nr_delegate << " NUMBER_DELEGATES " << NUMBER_DELEGATES << std::endl;
#endif
        // Log an error.
		if (this_l->connection->node->config.logging.bulk_pull_logging ())
		{
		    BOOST_LOG (this_l->connection->node->log) << "number of delegates does not match: server: " << NUMBER_DELEGATES << " client: " << nr_delegate;
		}
    }
}
