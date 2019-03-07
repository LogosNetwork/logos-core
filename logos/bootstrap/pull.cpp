#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/p2p.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>
#include <logos/bootstrap/pull.hpp>

#define _DEBUG 1

extern std::atomic<int> total_pulls;

logos::pull_info::pull_info () :
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
type(pull_type::batch_block_pull)
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
    LOG_DEBUG(logos::bootstrap_get_logger()) << "logos::pull_info::pull_info: delegate_id: " << delegate_id << std::endl;
}

logos::bulk_pull_client::bulk_pull_client (std::shared_ptr<logos::bootstrap_client> connection_a, logos::pull_info const & pull_a) :
connection (connection_a),
pull (pull_a)
{
    std::lock_guard<std::mutex> mutex (connection->attempt->mutex);
    ++connection->attempt->pulling;
    connection->attempt->condition.notify_all ();
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::bulk_pull_client:: total_pulls: " << total_pulls << std::endl;
    total_pulls++;
}

logos::bulk_pull_client::~bulk_pull_client ()
{
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::~bulk_pull_client" << std::endl;
    std::lock_guard<std::mutex> mutex (connection->attempt->mutex);
    --connection->attempt->pulling;
    connection->attempt->condition.notify_all ();
    if(total_pulls > 0) total_pulls--;
}

void logos::bulk_pull_client::request_batch_block()
{
  // NOTE: Called in 'logos::bootstrap_attempt::request_pull'
  // NOTE: Calls 'bulk_pull_client::receive_block' which calls 'bulk_pull_client::received_type', which calls 'bulk_pull_client::received_block'
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::request_batch_block delegate_id: " << pull.delegate_id << std::endl;
    logos::bulk_pull    req;
    req.type            = logos::message_type::batch_blocks_pull;
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

    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::request_batch_block delegate_id: " << pull.delegate_id << " b_start: " << pull.b_start.to_string() << " b_end: " << pull.b_end.to_string() << std::endl;

    LOG_DEBUG(connection->node->log) << " logos::bulk_pull_client::request_batch_block::pull: " << req << std::endl;

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
        req.serialize (stream); // NOTE Serialize has been implemented to support new fields.
    }

    auto this_l (shared_from_this ());
    connection->start_timeout ();
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::request_batch_block async_write delegate_id: " << pull.delegate_id << std::endl;
    boost::asio::async_write (connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer](boost::system::error_code const & ec, size_t size_a) {
        this_l->connection->stop_timeout ();
        if (!ec)
        {
            this_l->receive_block ();
        }
        else
        {
            LOG_DEBUG(this_l->connection->node->log) << "logos::bulk_pull_client::request_batch_block bulk_pull_client: delegate_id: " << this_l->pull.delegate_id << " line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
            if (this_l->connection->node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (this_l->connection->node->log) << boost::str (boost::format ("Error sending bulk pull request to %1%: to %2%") % ec.message () % this_l->connection->endpoint);
            }
            //this_l->connection->socket.close();
            if(total_pulls > 0) total_pulls--;
        }
    });
}

void logos::bulk_pull_client::receive_block ()
{
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::receive_block" << std::endl;

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
                LOG_INFO (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type: %1%") % ec.message ());
            }
            LOG_DEBUG(this_l->connection->node->log) << "logos::bulk_pull_client::receive_block: bulk_pull_client: delegate_id: " << this_l->pull.delegate_id << " line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
            //this_l->connection->socket.close();
            if(total_pulls > 0) total_pulls--;
        }
    });
}

void logos::bulk_pull_client::received_type ()
{
    auto this_l (shared_from_this ());
    logos::block_type type (static_cast<logos::block_type> (connection->receive_buffer[0]));
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_type: " << (int)type << " delegate_id: " << pull.delegate_id << std::endl;
    switch (type)
    {
        case logos::block_type::batch_block:
        {
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_type: logos::block_type::batch_block" << std::endl;
            connection->start_timeout ();
            boost::asio::async_read (connection->socket, boost::asio::buffer
                //(connection->receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response) - 1),
                (connection->receive_buffer.data () + 1, 7),
                [this_l](boost::system::error_code const & ec, size_t size_a) {
                    this_l->connection->stop_timeout ();
                    this_l->received_block_size (ec, size_a);
            });
            break;
        }
        case logos::block_type::micro_block:
        {
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_type: logos::block_type::micro_block" << std::endl;
            connection->start_timeout ();
            boost::asio::async_read (connection->socket, boost::asio::buffer 
                //(connection->receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response_micro) - 1),
                (connection->receive_buffer.data () + 1, 7),
                [this_l](boost::system::error_code const & ec, size_t size_a) {
                    this_l->connection->stop_timeout ();
                    this_l->received_block_size (ec, size_a);
            });
            break;
        }
        case logos::block_type::epoch_block:
        {
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_type: logos::block_type::epoch_block" << std::endl;
            connection->start_timeout ();
            boost::asio::async_read (connection->socket, boost::asio::buffer 
                //(connection->receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response_epoch) - 1), 
                (connection->receive_buffer.data () + 1, 7),
                [this_l](boost::system::error_code const & ec, size_t size_a) {
                    this_l->connection->stop_timeout ();
                    this_l->received_block_size (ec, size_a);
            });
            break;
        }
        case logos::block_type::not_a_block:
        {
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_type: logos::block_type::not_a_block" << std::endl;
            // Avoid re-using slow peers, or peers that sent the wrong blocks.
#ifndef _DEBUG
            if (!connection->pending_stop && expected == end_transmission) // RESEARCH
            {
                connection->attempt->pool_connection (connection);
            }
#else
            connection->attempt->pool_connection (connection); // FIXME
#endif

            if(connection->node->_validator->validate(connection->attempt, nullptr)) {
                if (connection->node->config.logging.bulk_pull_logging ())
                {
                    LOG_INFO (connection->node->log) << " bulk_pull_client::received_block got invalid batch block ";
                }
#ifndef _DEBUG
                connection->stop(true); // FIXME
#endif
            }
            break;
        }
        default:
        {
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_type: default: received unknown type block: " << (int)type << " delegate_id: " << pull.delegate_id << std::endl;
            if (connection->node->config.logging.network_packet_logging ())
            {
                LOG_INFO (connection->node->log) << boost::str (boost::format ("Unknown type received as block type: %1%") % static_cast<int> (type));
            }
            break;
        }
    }
}

void logos::bulk_pull_client::received_block_size(boost::system::error_code const & ec, size_t size_a)
{
    auto this_l (shared_from_this ());
    if(!ec) {
        logos::block_type type (static_cast<logos::block_type> (connection->receive_buffer[0]));
        if(type == logos::block_type::batch_block ||
           type == logos::block_type::micro_block ||
           type == logos::block_type::epoch_block) {
            uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
            int block_size= *(int *)(data+4);
            connection->start_timeout ();
            boost::asio::async_read (connection->socket, boost::asio::buffer
                //(connection->receive_buffer.data () + 1, sizeof(BatchBlock::bulk_pull_response) - 1),
                (connection->receive_buffer.data () + 8, block_size - 8),
                [this_l](boost::system::error_code const & ec, size_t size_a) {
                    this_l->connection->stop_timeout ();
                    this_l->received_block(ec, size_a);
            });
        } else {
            LOG_INFO(connection->node->log) << "logos::bulk_pull_client::received_block_size: invalid type: " << (int)type << std::endl;
        }
    } else {
        LOG_INFO(connection->node->log) << "logos::bulk_pull_client::received_block_size: error: " << ec.message() << std::endl;
    }
}

void logos::bulk_pull_client::received_block (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        logos::block_type type (static_cast<logos::block_type> (connection->receive_buffer[0]));
        if(type == logos::block_type::batch_block) {
            uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response> block(new BatchBlock::bulk_pull_response);
            if(block) {
                logos::bufferstream stream(data,connection->receive_buffer.size());
                BatchBlock::bulk_pull_response::DeSerialize(stream, *block.get());
                BlockHash hash = block->block->Hash();
                LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_block batch block received: delegate_id: " << block->delegate_id << " " << "r->Hash(): " << hash.to_string() << std::endl;
                if (connection->node->config.logging.bulk_pull_logging ())
                {
                    LOG_INFO (connection->node->log) << " bulk_pull_client::received_block got block hash " << hash.to_string();
                }
                if (connection->block_count++ == 0)
                {
                    connection->start_time = std::chrono::steady_clock::now ();
                }
                connection->attempt->total_blocks++;

//TODO                block->peer = p2p::get_peer_id(connection->endpoint.address().to_v6().to_string());
                block->retry_count = 0; // initial time we get this block

                if(connection->node->_validator->validate(connection->attempt, block)) {
                    if (connection->node->config.logging.bulk_pull_logging ())
                    {
                        LOG_INFO (connection->node->log) << " bulk_pull_client::received_block got invalid batch block " << hash.to_string();
                    }
#ifndef _DEBUG
                    connection->stop(true); // FIXME
#endif
                }
                if (!connection->hard_stop.load ()) {
                    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_block: calling receive_block: " << std::endl;
                    expected = hash;
                    receive_block (); // NOTE: Read more blocks. This implements a loop.
                } else {
                    connection->socket.close();
                    connection->stop(true); // FIXME
                }
            }
        } else if(type == logos::block_type::micro_block) {
            uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response_micro> block(new BatchBlock::bulk_pull_response_micro);
            if(block) {
                logos::bufferstream stream(data,connection->receive_buffer.size());
                BatchBlock::bulk_pull_response_micro::DeSerialize(stream, *block.get());
                BlockHash hash = block->micro->Hash();
                if (connection->node->config.logging.bulk_pull_logging ())
                {
                    LOG_INFO (connection->node->log) << " bulk_pull_client::received_block got block hash " << hash.to_string();
                }
                if (connection->block_count++ == 0)
                {
                    connection->start_time = std::chrono::steady_clock::now ();
                }
                connection->attempt->total_blocks++;

//TODO                block->peer = p2p::get_peer_id(connection->endpoint.address().to_v6().to_string());
                block->retry_count = 0; // initial time we get this block
                connection->node->_validator->add_micro_block(connection->attempt, block);

                if (!connection->hard_stop.load ()) {
                    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_block: calling receive_block: "<< std::endl;
                    expected = hash;
                    receive_block (); // NOTE: Read more blocks. This implements a loop.
                } else {
                    connection->socket.close();
                    connection->stop(true); // FIXME
                }
            }
        } else if(type == logos::block_type::epoch_block) {
            uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
            std::shared_ptr<BatchBlock::bulk_pull_response_epoch> block(new BatchBlock::bulk_pull_response_epoch);
            if(block) {
                logos::bufferstream stream(data,connection->receive_buffer.size());
                BatchBlock::bulk_pull_response_epoch::DeSerialize(stream, *block.get());
                BlockHash hash = block->epoch->Hash();
                if (connection->node->config.logging.bulk_pull_logging ())
                {
                    LOG_INFO (connection->node->log) << " bulk_pull_client::received_block got block hash " << hash.to_string();
                }
                if (connection->block_count++ == 0)
                {
                    connection->start_time = std::chrono::steady_clock::now ();
                }
                connection->attempt->total_blocks++;

                LOG_DEBUG(connection->node->log) << " received_epoch: " << hash.to_string() << std::endl;
//TODO                block->peer = p2p::get_peer_id(connection->endpoint.address().to_v6().to_string());
                block->retry_count = 0; // initial time we get this block
                connection->node->_validator->add_epoch_block(connection->attempt, block);

                if (!connection->hard_stop.load ()) {
                    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_block: calling receive_block: epoch: "<< hash.to_string() << std::endl;
                    expected = hash;
                    receive_block (); // NOTE: Read more blocks. This implements a loop.
                } else {
                    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_block: calling stop: epoch: "<< hash.to_string() << std::endl;
                    connection->socket.close();
                    connection->stop(true); // FIXME
                }
            }
        } else {
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_block: error deserializing block delegate_id: " << pull.delegate_id << " line: " << __LINE__ << std::endl;
            if (connection->node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (connection->node->log) << "Error deserializing block received from pull request";
            }
        }
    }
    else
    {
        if (connection->node->config.logging.bulk_pull_logging ())
        {
            LOG_INFO (connection->node->log) << boost::str (boost::format ("Error bulk receiving block: %1%") % ec.message ());
        }
        LOG_DEBUG(connection->node->log) << "logos::bulk_pull_client::received_block: receive error: bulk_pull_client: delegate_id: " << pull.delegate_id << " line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
        //connection->socket.close();
        if(total_pulls > 0) total_pulls--;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Handle a request for the pull of all blocks (bsb,micro,epoch) associated with a delegate.
 */
void logos::bulk_pull_server::set_current_end ()
{ // NOTE: Called from logos::bulk_pull_server::bulk_pull_server
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::set_current_end: delegate_id: " << request->delegate_id << " start current_bsb: " << request->b_start.to_string() << std::endl;

    // Setup current_micro and current_bsb for iterating micro and bsb blocks.
    current_epoch   = request->e_start;
    if(current_epoch == request->e_end && !current_epoch.is_zero())//TODO bootstrap from beginning
    {
        while(true) {
           BlockHash previous = EpochBlock::getPrevEpochBlock(connection->node->store, current_epoch);
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
           BlockHash previous = Micro::getPrevMicroBlock(connection->node->store, current_micro);
           if(previous.is_zero()) {
                break;
           }
           current_micro = previous; // Walk backwards till the beginning...
        }
    }
    
    current_bsb     = request->b_start;
    if(current_bsb == request->b_end && !current_bsb.is_zero())
    {
        LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::set_current_end: walking back chain for delegate: " << request->delegate_id << std::endl;
        while(true) {
           BlockHash previous = BatchBlock::getPrevBatchStateBlock(connection->node->store, request->delegate_id, current_bsb);
           if(previous.is_zero()) {
                break;
           }
           current_bsb = previous; // Walk backwards till the beginning...
        }
    }


    //TODO work around for a consensus bug where chains are broken
/*
    // Special case...
    BlockHash b_start = request->b_end;
    BlockHash current_previous = BatchBlock::getPrevBatchStateBlock(connection->node->store, request->delegate_id, current_bsb);
    if(current_previous.is_zero()) {
        while(true) {
            BlockHash previous = BatchBlock::getPrevBatchStateBlock(connection->node->store, request->delegate_id, b_start);
            BlockHash next = BatchBlock::getNextBatchStateBlock(connection->node->store, request->delegate_id, previous);
            if(next != b_start || previous.is_zero()) {
                current_bsb = b_start;
                break;
            }
            b_start = previous; // Walk backwards till we reach the beginning...
        }
    }
*/

    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::set_current_end: current_epoch: " << current_epoch.to_string() << " current_micro: " << current_micro.to_string() << " current_bsb: " << current_bsb.to_string() << " delegate_id: " << request->delegate_id << std::endl;
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::set_current_end: e_end: " << request->e_end.to_string() << " m_end: " << request->m_end.to_string() << " b_end: " << request->b_end.to_string() << " delegate_id: " << request->delegate_id << std::endl;
}

void logos::bulk_pull_server::send_next ()
{
    BlockHash zero = 0;
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::send_next: " << std::endl;
    {
        if(!current_epoch.is_zero()) { 
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::send_next: epoch_block" << std::endl;
            std::shared_ptr<ApprovedEB> e = EpochBlock::readEpochBlock(connection->node->store, current_epoch);
            BatchBlock::bulk_pull_response_epoch resp;
            resp.delegate_id = request->delegate_id; // m->delegateNumber;
            LOG_DEBUG(connection->node->log) << "addr: " << (uint64_t)&resp.epoch << " src: " << (uint64_t)e.get() << " size: " << sizeof(Epoch) << std::endl;
            if(e == nullptr) {
                LOG_DEBUG(connection->node->log) << " null return: " << current_epoch.to_string() << std::endl;
                current_epoch = zero;
                send_next();
                return;
            }
            try {
            resp.epoch = e; // Assign shared pointer to serialize later.
            if(current_epoch == request->e_end) {
                current_epoch = zero; // We are at the end...
            } else {
                BlockHash epoch_block = EpochBlock::getNextEpochBlock(connection->node->store, current_epoch);
                current_epoch = epoch_block;
            }

            auto send_buffer1(std::make_shared<std::vector<uint8_t>>());
            {
                logos::vectorstream stream(*send_buffer1.get());
                resp.Serialize(stream);
            }

            LOG_DEBUG(connection->node->log) << " sending epoch: " << e->Hash().to_string() << std::endl;
            auto this_l (shared_from_this ());
            if (connection->node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % e->Hash ().to_string ());
            }
            ((BatchBlock::bulk_pull_response_epoch *)send_buffer1->data())->block_size = send_buffer1->size(); // Record wire size...
            int size = send_buffer1->size();
            async_write (*connection->socket, boost::asio::buffer (send_buffer1->data (), size), [this_l,send_buffer1](boost::system::error_code const & ec, size_t size_a) {
                this_l->sent_action (ec, size_a);
            });
            } catch(std::exception &e) {
                std::cout << "caught exception: " << e.what() << std::endl;
            } catch(...) {
                std::cout << "caught unknown exception: " << std::endl;
            } // RGD
        } else if(!current_micro.is_zero()) {
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::send_next: micro_block" << std::endl;
            std::shared_ptr<ApprovedMB> m = Micro::readMicroBlock(connection->node->store, current_micro);
            BatchBlock::bulk_pull_response_micro resp;
            resp.delegate_id = request->delegate_id; // m->delegateNumber;
            if(m == nullptr) {
                current_micro = zero;
                send_next();
                return;
            }
            try {
            resp.micro = m; // Assign shared pointer for serialization later.
            if(current_micro == request->m_end) {
                current_micro = zero;
            } else {
                BlockHash micro_block = Micro::getNextMicroBlock(connection->node->store, current_micro);
                current_micro = micro_block;
            }

            auto send_buffer1(std::make_shared<std::vector<uint8_t>>());
            {
                logos::vectorstream stream(*send_buffer1.get());
                resp.Serialize(stream);
            }

            auto this_l (shared_from_this ());
            if (connection->node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % m->Hash ().to_string ());
            }
            ((BatchBlock::bulk_pull_response_micro *)send_buffer1->data())->block_size = send_buffer1->size(); // Record wire size...
            int size = send_buffer1->size();
            async_write (*connection->socket, boost::asio::buffer (send_buffer1->data (), size), [this_l,send_buffer1](boost::system::error_code const & ec, size_t size_a) {
                this_l->sent_action (ec, size_a);
            });
            } catch(std::exception &e) {
                std::cout << "caught exception: " << e.what() << std::endl;
            } catch(...) {
                std::cout << "caught unknown exception: " << std::endl;
            } // RGD
        } else {
            LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::send_next: bsb_block" << std::endl;
           // Get BSB blocks. 
           BlockHash bsb = BatchBlock::getNextBatchStateBlock(connection->node->store, request->delegate_id, current_bsb);
           static int total_count = 0;
           total_count++;
           if(bsb.is_zero())
           {
                LOG_DEBUG(connection->node->log) << "bsb is_zero" << std::endl;
           }
           else
           {
                LOG_DEBUG(connection->node->log) << "bsb is_non_zero: current_bsb: " << current_bsb.to_string() <<  " delegate_id: " << request->delegate_id << std::endl;
           }

           if(!current_bsb.is_zero())
           {
                LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server:: total_count: " << total_count << " delegate_id: " << request->delegate_id << std::endl; 
                LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server:: count: " << iter_count << " delegate_id: " << request->delegate_id << std::endl; 
                std::shared_ptr<ApprovedBSB> b = BatchBlock::readBatchStateBlock(connection->node->store, current_bsb);
                BatchBlock::bulk_pull_response resp;
                resp.delegate_id = request->delegate_id;
                if(b == nullptr) {
                    current_bsb = zero;
                    send_next();
                    return;
                }
                resp.block = b; // Assign shared pointer for use in serialization
                LOG_DEBUG(connection->node->log) << " current_bsb: " << current_bsb.to_string() << " << b->Hash().to_string() " << b->Hash().to_string()  << " message_count: " << b->block_count << " delegate_id: " << request->delegate_id << std::endl;
                LOG_DEBUG(connection->node->log) << " is_non_zero: current_bsb: " << current_bsb.to_string() <<  " delegate_id: " << request->delegate_id << " request.b_end: " << request->b_end.to_string() << std::endl;
                try {
                if(current_bsb == request->b_end) {
                    BlockHash zero = 0;
                    current_bsb = zero; // We are at the end...
                } else {
                    current_bsb = bsb;
                }

                auto send_buffer1(std::make_shared<std::vector<uint8_t>>());
                {
                    logos::vectorstream stream(*send_buffer1.get());
                    resp.Serialize(stream);
                }

                auto this_l (shared_from_this ());
                if (connection->node->config.logging.bulk_pull_logging ())
                {
                    LOG_INFO (connection->node->log) << boost::str (boost::format ("Sending block: %1%") % b->Hash ().to_string ());
                }
                ((BatchBlock::bulk_pull_response *)send_buffer1->data())->block_size = send_buffer1->size(); // Record wire size...
                int size = send_buffer1->size();
                async_write (*connection->socket, boost::asio::buffer (send_buffer1->data (), size), [this_l,send_buffer1](boost::system::error_code const & ec, size_t size_a) {
                    this_l->sent_action (ec, size_a);
                });
                } catch(std::exception &e) {
                    std::cout << "caught exception: " << e.what() << std::endl;
                } catch(...) {
                    std::cout << "caught unknown exception: " << std::endl;
                } // RGD
           } else {
                LOG_DEBUG(connection->node->log) << "send_finished: current_bsb: " << current_bsb.to_string() << " request_end: " << request->b_end.to_string() << " delegate_id: " << request->delegate_id << std::endl;
                send_finished ();
           }
        }
    }
}

void logos::bulk_pull_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::sent_action delegate_id:" << request->delegate_id << " size_a: " << size_a << std::endl;

    if (!ec)
    {
        LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::sent_action:: send_next" << std::endl;
        send_next ();
    }
    else
    {
        LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::sent_action:: error: message: " << ec.message() << std::endl;
        if (connection->node->config.logging.bulk_pull_logging ())
        {
            LOG_INFO (connection->node->log) << boost::str (boost::format ("Unable to bulk send block: %1%") % ec.message ());
        }
        connection->socket->close();
        connection->finish_request ();
    }
}

void logos::bulk_pull_server::send_finished ()
{
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::send_finished delegate_id:" << request->delegate_id << std::endl;
    send_buffer.clear ();
    send_buffer.push_back (static_cast<uint8_t> (logos::block_type::not_a_block));
    auto this_l (shared_from_this ());
    if (connection->node->config.logging.bulk_pull_logging ())
    {
        LOG_INFO (connection->node->log) << "Bulk sending finished";
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
        this_l->no_block_sent (ec, size_a);
    });
}

void logos::bulk_pull_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::no_block_sent: " << std::endl;
    if (!ec)
    {
        assert (size_a == 1);
        LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::no_block_sent: finish_request: delegate_id: " << request->delegate_id << std::endl;
        connection->finish_request ();
    }
    else
    {
        LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::no_block_sent: finish_request: error: delegate_id: " << request->delegate_id << " ec.message: " << ec.message() << std::endl;
        //connection->finish_request (); // MINE
        if (connection->node->config.logging.bulk_pull_logging ())
        {
            LOG_INFO (connection->node->log) << "Unable to send not-a-block";
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
    LOG_DEBUG(connection->node->log) << "logos::bulk_pull_server::bulk_pull_server: delegate_id: " << request->delegate_id << std::endl;
    set_current_end ();
}
