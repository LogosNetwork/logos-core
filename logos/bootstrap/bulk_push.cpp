// NOTE This code is disabled...
#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_push.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>

#define MAX_ITER 4

#define _DEBUG 1

logos::bulk_push_client::bulk_push_client (std::shared_ptr<logos::bootstrap_client> const & connection_a) :
connection (connection_a)
{
}

logos::bulk_push_client::~bulk_push_client ()
{
}

void logos::bulk_push_client::start ()
{ // RGD: Called from 'request_push'
  // RGD: Calls bulk_push_client::push
	logos::bulk_push message;
    request_id = 0;
    iter_count = 0;

#ifdef _DEBUG
    std::cout << "logos::bulk_push_client::start: size: " << connection->attempt->req.size() << " {" << std::endl;
    for(int i = 0; i < connection->attempt->req.size(); ++i) {
        std::cout << "logos::bulk_push_client::start: delegate_id: " << connection->attempt->req[i].delegate_id << std::endl;
    }
    std::cout << "logos::bulk_push_client::start: }" << std::endl;
#endif

    request = &connection->attempt->req[request_id];

    if(request) {
        current_epoch = request->e_start;
        current_micro = request->m_start;
        current_bsb   = request->b_start;
    }

	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	{
		logos::vectorstream stream (*buffer);
		message.serialize (stream);
	}
	auto this_l (shared_from_this ());
	connection->start_timeout ();
	boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer](boost::system::error_code const & ec, size_t size_a) {
		this_l->connection->stop_timeout ();
		logos::transaction transaction (this_l->connection->node->store.environment, nullptr, false);
		if (!ec)
		{
			this_l->push (transaction);
		}
		else
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_push_client::start: network error: " << ec.message() << " delegate_id: " << this_l->request->delegate_id << std::endl;
#endif
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Unable to send bulk_push request: %1%") % ec.message ());
			}
            this_l->promise.set_value(true);
		}
	});
}

void logos::bulk_push_client::push (MDB_txn * transaction_a)
{ // RGD: Called from 'logos::bulk_push_client::start'
	{
        //for(int i = 0; i < connection->attempt->req.size(); ++i)
        if(request_id <= connection->attempt->req.size()) {
            if(!current_epoch.is_zero() && current_epoch != request->e_end) {
                auto send_buffer(std::make_shared<std::vector<uint8_t>>(BatchBlock::bulk_pull_response_mesg_len, uint8_t(0)));
	            BlockHash epoch_block = Micro::getNextMicroBlock(connection->node->store, request->delegate_id, current_epoch);
	            if(!epoch_block.is_zero() && epoch_block != request->e_end) {
	                std::shared_ptr<Epoch> e = EpochBlock::readEpochBlock(connection->node->store, epoch_block);
	                if(!e) {
                        send_finished(); // End the transmission on memory error.
	                }
	                BatchBlock::bulk_pull_response_epoch resp;
	                resp.delegate_id = request->delegate_id; // m->delegateNumber;
	                memcpy(&resp.epoch,&(*e),sizeof(Epoch));
	                current_epoch = epoch_block; // Advance pointer...
	
	                {
                       memcpy(send_buffer->data(),(void *)&resp, sizeof(resp));
	                }
	
			        auto this_l (shared_from_this ());
			        if (connection->node->config.logging.bulk_pull_logging ())
			        {
				        BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % e->Hash ().to_string ());
			        }
			        async_write (connection->socket, boost::asio::buffer (send_buffer->data(), sizeof(BatchBlock::bulk_pull_response_micro)), [this_l](boost::system::error_code const & ec, size_t size_a) {
                        if(!ec)
			            {
				            this_l->send_next();
			            } else
                        {
#ifdef _DEBUG
                            std::cout << "logos::bulk_push_client::push: network error size: " << this_l->connection->attempt->req.size() << std::endl;
                            std::cout << "logos::bulk_push_client::push: network error: " << ec.message() << " delegate_id: " << this_l->request->delegate_id << std::endl;
#endif
                            this_l->promise.set_value (true);
                        }
			        });
	            }
            } else if(!current_micro.is_zero() && current_micro != request->m_end) {
                auto send_buffer(std::make_shared<std::vector<uint8_t>>(BatchBlock::bulk_pull_response_mesg_len, uint8_t(0)));
	            BlockHash micro_block = Micro::getNextMicroBlock(connection->node->store, request->delegate_id, current_micro);
	            if(!micro_block.is_zero() && micro_block != request->m_end) {
	                std::shared_ptr<MicroBlock> m = Micro::readMicroBlock(connection->node->store, micro_block);
	                if(!m) {
                        send_finished(); // End the transmission on memory error.
	                }
	                BatchBlock::bulk_pull_response_micro resp;
	                resp.delegate_id = request->delegate_id; // m->delegateNumber;
	                memcpy(&resp.micro,&(*m),sizeof(MicroBlock));
	                current_micro = micro_block; // Advance pointer...
	
	                {
                       memcpy(send_buffer->data(),(void *)&resp, sizeof(resp));
	                }
	
			        auto this_l (shared_from_this ());
			        if (connection->node->config.logging.bulk_pull_logging ())
			        {
				        BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % m->Hash ().to_string ());
			        }
			        async_write (connection->socket, boost::asio::buffer (send_buffer->data(), sizeof(BatchBlock::bulk_pull_response_micro)), [this_l](boost::system::error_code const & ec, size_t size_a) {
                        if(!ec)
			            {
				            this_l->send_next();
			            } else
                        {
#ifdef _DEBUG
                            std::cout << "logos::bulk_push_client::push: network error size: " << this_l->connection->attempt->req.size() << std::endl;
                            std::cout << "logos::bulk_push_client::push: network error: " << ec.message() << " delegate_id: " << this_l->request->delegate_id << std::endl;
#endif
                            this_l->promise.set_value (true);
                        }
			        });
	            }
	        } 
#ifdef _DEBUG
            if(iter_count++ < MAX_ITER)
#else
	        //while(!current_bsb.is_zero())
            else if(!current_bsb.is_zero() && current_bsb != request->b_end)
#endif
            {
               auto send_buffer(std::make_shared<std::vector<uint8_t>>(BatchBlock::bulk_pull_response_mesg_len, uint8_t(0)));
	           // Get BSB blocks. 
	           BlockHash bsb = BatchBlock::getNextBatchStateBlock(connection->node->store, request->delegate_id, current_bsb);
#ifndef _DEBUG
	           if(!bsb.is_zero() && bsb != request->b_end)
#endif
               {
	                std::shared_ptr<BatchStateBlock> b = BatchBlock::readBatchStateBlock(connection->node->store, bsb);
	                if(!b) {
                        send_finished(); // End the transmission on memory error.
	                }
	                BatchBlock::bulk_pull_response resp;
	                resp.delegate_id = request->delegate_id;
	                memcpy(&resp.block,&(*b),sizeof(BatchStateBlock));
	                current_bsb = bsb; // Advance pointer
	
		            {
                        memcpy(send_buffer->data(),(void *)&resp, sizeof(resp));
		            }

				    auto this_l (shared_from_this ());
				    if (connection->node->config.logging.bulk_pull_logging ())
				    {
					    BOOST_LOG (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % b->Hash ().to_string ());
				    }
                    int size = (sizeof(BatchBlock::bulk_pull_response));
				    async_write (connection->socket, boost::asio::buffer (send_buffer->data(), size), [this_l,send_buffer](boost::system::error_code const & ec, size_t size_a) {
                        if(!ec)
			            {
                            this_l->send_next();
			            } else {
#ifdef _DEBUG
                            std::cout << "logos::bulk_push_client::push: network error size: " << this_l->connection->attempt->req.size() << std::endl;
                            //std::cout << "logos::bulk_push_client::push: network error: " << ec.message() << " delegate_id: " << this_l->request->delegate_id << std::endl;
#endif
                            this_l->promise.set_value (true);
                        }
				    });
	            }
	        }
        }
	}
}

void logos::bulk_push_client::send_next ()
{
#ifdef _DEBUG
    if(iter_count >= MAX_ITER)
#else
    if(current_epoch.is_zero() && current_micro.is_zero() && current_bsb.is_zero())
#endif
    {
        // Do we have more to send...
        request_id++;
#ifdef _DEBUG
        std::cout << "logos::bulk_push_client::send_next::request_id: " << request_id << " req_s.size: " << connection->attempt->req.size() << std::endl;
#endif
        iter_count = 0;
        if(request_id > connection->attempt->req.size()) {
            send_finished();
        } else {
            request = &connection->attempt->req[request_id]; // Process all requests in the vector...
            if(request) {
                current_epoch = request->e_start;
                current_micro = request->m_start;
                current_bsb   = request->b_start;
            }
		    logos::transaction transaction (connection->node->store.environment, nullptr, false);
            push(transaction);
        }
    } else {
	    logos::transaction transaction (connection->node->store.environment, nullptr, false);
        push(transaction);
    }
}

void logos::bulk_push_client::send_finished ()
{
#ifdef _DEBUG
    std::cout << "logos::bulk_push_client::send_finished: " << std::endl;
#endif

	auto buffer (std::make_shared<std::vector<uint8_t>> ());
	buffer->push_back (static_cast<uint8_t> (logos::block_type::not_a_block));
	if (connection->node->config.logging.network_logging ())
	{
		BOOST_LOG (connection->node->log) << "Bulk push finished";
	}
	auto this_l (shared_from_this ());
	async_write (connection->socket, boost::asio::buffer (buffer->data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		try
		{
			this_l->promise.set_value (false);
		}
		catch (std::future_error &)
		{
		}
	});
}

// RGDPUSH now the server receives (where as in the bulk_pull, the client receives).
logos::bulk_push_server::bulk_push_server (std::shared_ptr<logos::bootstrap_server> const & connection_a) :
connection (connection_a)
{
}

void logos::bulk_push_server::receive ()
{
	auto this_l (shared_from_this ());
	boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
		if (!ec)
		{
			this_l->received_type ();
		}
		else
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_push_server::receive: networ error: " << ec.message() << std::endl;
#endif
			if (this_l->connection->node->config.logging.bulk_pull_logging ())
			{
				BOOST_LOG (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type: %1%") % ec.message ());
			}
		}
	});
}

void logos::bulk_push_server::received_type ()
{
#ifdef _DEBUG
    std::cout << "logos::bulk_push_server::received_type: " << std::endl;
#endif

	auto this_l (shared_from_this ());
	logos::block_type type (static_cast<logos::block_type> (receive_buffer[0]));
	switch (type)
	{
		case logos::block_type::batch_block:
		{
			boost::asio::async_read (*connection->socket, boost::asio::buffer 
                (receive_buffer.data() + 1, sizeof(BatchBlock::bulk_pull_response) - 1),
                [this_l](boost::system::error_code const & ec, size_t size_a) {
				    this_l->received_block (ec, size_a);
			});
			break;
		}
		case logos::block_type::micro_block:
		{
			boost::asio::async_read (*connection->socket, boost::asio::buffer 
                (receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response_micro) - 1), 
                [this_l](boost::system::error_code const & ec, size_t size_a) {
				    this_l->received_block (ec, size_a);
			});
			break;
		}
		case logos::block_type::epoch_block:
        {
			boost::asio::async_read (*connection->socket, boost::asio::buffer 
                (receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response_epoch) - 1),
                [this_l](boost::system::error_code const & ec, size_t size_a) {
				    this_l->received_block (ec, size_a);
			});
			break;
		}
case logos::block_type::state:
		{
			connection->node->stats.inc (logos::stat::type::bootstrap, logos::stat::detail::state_block, logos::stat::dir::in);
			boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, logos::state_block::size), [this_l](boost::system::error_code const & ec, size_t size_a) {
				this_l->received_block (ec, size_a);
			});
			break;
		}
		case logos::block_type::not_a_block:
		{
            if(connection->node->_validator->validate(nullptr)) {
                // FIXME Should we do something in addition to finishing the request ?
			    if (connection->node->config.logging.bulk_pull_logging ())
			    {
			        BOOST_LOG (connection->node->log) << " bulk_pull_client::received_block got invalid batch block ";
			    }
#ifndef _DEBUG
                connection->stop(true);
#endif
            }
			connection->finish_request();
			break;
		}
		default:
		{
#ifdef _DEBUG
            std::cout << "logos::bulk_push_server::received_type: unknown block type" << std::endl;
#endif
			if (connection->node->config.logging.network_packet_logging ())
			{
				BOOST_LOG (connection->node->log) << "Unknown type received as block type";
			}
			break;
		}
	}
}

void logos::bulk_push_server::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
	    logos::block_type type (static_cast<logos::block_type> (receive_buffer[0]));
        if(type == logos::block_type::batch_block) {
			uint8_t *data = receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response> block(new BatchBlock::bulk_pull_response);
            if(block) {
                memcpy(&(*block),data,sizeof(BatchBlock::bulk_pull_response));
                BlockHash hash = block->block.Hash();
#ifdef _DEBUG
                std::cout << "logos::bulk_push_server::received_block delegate_id: " << block->delegate_id << std::endl;
#endif
				if (connection->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (connection->node->log) << " bulk_push_server::received_block got block hash " << hash.to_string();
				}
                if(connection->node->_validator->validate(block)) {
				    if (connection->node->config.logging.bulk_pull_logging ())
				    {
					    BOOST_LOG (connection->node->log) << " bulk_push_server::received_block got invalid batch block " << hash.to_string();
				    }
#ifndef _DEBUG
                    connection->stop(true);
#endif
                }
				receive(); // RGD: Read more blocks if available...
            }
        } else if(type == logos::block_type::micro_block) {
			uint8_t *data = receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response_micro> block(new BatchBlock::bulk_pull_response_micro);
            if(block) {
                memcpy(&(*block),data,sizeof(BatchBlock::bulk_pull_response_micro));
                BlockHash hash = block->micro.Hash();
				if (connection->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (connection->node->log) << " bulk_pull_client::received_block got block hash " << hash.to_string();
				}
                connection->node->_validator->add_micro_block(block);
				receive(); // RGD: Read more blocks if available...
            }
        } else if(type == logos::block_type::epoch_block) {
			uint8_t *data = receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response_epoch> block(new BatchBlock::bulk_pull_response_epoch);
            if(block) {
                memcpy(&(*block),data,sizeof(BatchBlock::bulk_pull_response_epoch));
                BlockHash hash = block->epoch.Hash();
				if (connection->node->config.logging.bulk_pull_logging ())
				{
					BOOST_LOG (connection->node->log) << " bulk_pull_client::received_block got block hash " << hash.to_string();
				}
                connection->node->_validator->add_epoch_block(block);
				receive(); // RGD: Read more blocks if available...
            }
        } else {
#ifdef _DEBUG
            std::cout << "logos::bulk_push_server::received_block error" << std::endl;
#endif
		    logos::bufferstream stream (receive_buffer.data (), 1 + size_a);
		    auto block (logos::deserialize_block (stream));
		    if (block != nullptr && !logos::work_validate (*block))
		    {
			    connection->node->process_active (std::move (block));
			    receive ();
		    }
		    else
		    {
			    if (connection->node->config.logging.bulk_pull_logging ())
			    {
				    BOOST_LOG (connection->node->log) << "Error deserializing block received from pull request";
			    }
		    }
	    }
    } else {
#ifdef _DEBUG
        std::cout << "logos::bulk_push_server::received_block: network error: " << ec.message() << std::endl;
#endif
    }
}
