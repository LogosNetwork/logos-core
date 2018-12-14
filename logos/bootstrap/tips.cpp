#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/bulk_pull.hpp>
#include <logos/bootstrap/batch_block_tips.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/epoch.hpp>
#include <logos/bootstrap/batch_block_validator.hpp>

#include <logos/node/common.hpp>
#include <logos/node/node.hpp>

#include <boost/log/trivial.hpp>

extern std::atomic<int> total_pulls;

void logos::tips_req_client::run ()
{ // NOTE: Called from 'logos::bootstrap_attempt::request_tips'
  // tips This is the start of it. We write the request as a request->serialize
  // and kick off the process...
    LOG_DEBUG(connection->node->log) << "tips_req_client::run" << std::endl;

    if(connection->attempt->pulling > 0 || total_pulls > 0) {
        try {
            promise.set_value(false);
        } catch(const std::future_error &e)
        {
            LOG_DEBUG(connection->node->log) << "logos::tips_req_client::run: caught error in setting promise: " << e.what() << std::endl;
        }
        LOG_DEBUG(connection->node->log) << "logos::tips_req_client:: total_pulls: " << total_pulls << std::endl;
        LOG_DEBUG(connection->node->log) << "logos::tips_req_client::run: still pending" << std::endl;
        //connection->attempt->pool_connection(connection); // Don't pool_connection or we will deadlock...
        return;
    }
    LOG_DEBUG(connection->node->log) << "logos::tips_req_client:: total_pulls: " << total_pulls << std::endl;
    std::unique_ptr<logos::frontier_req> request (new logos::frontier_req);
    request->start.clear ();
    request->age = std::numeric_limits<decltype (request->age)>::max ();
    request->count = std::numeric_limits<decltype (request->age)>::max ();
    request->nr_delegate = NUMBER_DELEGATES; // TODO: Use same constant instead of hard-code.

    LOG_DEBUG(connection->node->log) << "::run count: " << request->count << " age: " << request->age << " nr_delegate: " << request->nr_delegate << " NUMBER_DELEGATES " << NUMBER_DELEGATES << std::endl;

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
            LOG_DEBUG(this_l->connection->node->log) << "this_l->receive_tips_header:" << std::endl;
            this_l->receive_tips_header ();
        }
        else
        {
            if (this_l->connection->node->config.logging.network_logging ())
            {
                LOG_INFO (this_l->connection->node->log) << boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ());
            }
            try {
                LOG_DEBUG(this_l->connection->node->log) << "tips:: line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
                this_l->promise.set_value(true); // Report the error to caller in bootstrap.cpp.
            } catch(const std::future_error &e)
            {
                LOG_DEBUG(this_l->connection->node->log) << "logos::tips_req_client::run: caught error in setting promise: " << e.what() << std::endl;
            }
        }
    });
}

logos::tips_req_client::tips_req_client (std::shared_ptr<logos::bootstrap_client> connection_a) :
connection (connection_a),
current (0),
count (0)
{
    logos::transaction transaction (connection->node->store.environment, nullptr, false);
    next (transaction);
}

logos::tips_req_client::~tips_req_client ()
{
    LOG_DEBUG(connection->node->log) << "logos::tips_req_client::~tips_req_client" << std::endl;
}

void logos::tips_req_client::receive_tips_header ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (connection->receive_buffer.data (), 1), [this_l](boost::system::error_code const & ec, size_t size_a) {
        if (!ec)
        {
            logos::block_type type (static_cast<logos::block_type> (this_l->connection->receive_buffer[0]));
            if(type == logos::block_type::frontier_block) {
                LOG_DEBUG(this_l->connection->node->log) << "received_batch_block_tips" << std::endl;
                boost::asio::async_read (this_l->connection->socket, boost::asio::buffer 
                    (this_l->connection->receive_buffer.data() + 1, sizeof(BatchBlock::tips_response) - 1),
                    [this_l](boost::system::error_code const & ec, size_t size_a) {
                        this_l->received_batch_block_tips(ec, size_a);
                });
            } else {
                LOG_DEBUG(this_l->connection->node->log) << "received_tips" << std::endl;
                this_l->receive_tips();
            }
        }
        else
        {
            if (this_l->connection->node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (this_l->connection->node->log) << boost::str (boost::format ("Error receiving block type: %1%") % ec.message ());
            }
            try {
                LOG_DEBUG(this_l->connection->node->log) << "tips:: line: " << __LINE__ << " ec.message: " << ec.message() << std::endl;
                this_l->promise.set_value(true);
            } catch(const std::future_error &e)
            {
                LOG_DEBUG(this_l->connection->node->log) << "logos::tips_req_client::receive_tips_header: caught error in setting promise: " << e.what() << std::endl;
            }
        }
    });
}

void logos::tips_req_client::receive_tips ()
{ // NOTE: Called from tips_req_client::run
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
                LOG_INFO (this_l->connection->node->log) << boost::str (boost::format ("Invalid size: expected %1%, got %2%") % size_l % size_a);
            }
            try {
                LOG_DEBUG(this_l->connection->node->log) << "tips:: line: " << __LINE__ << std::endl;
                this_l->promise.set_value(true);
            } catch(const std::future_error &e)
            {
                LOG_DEBUG(this_l->connection->node->log) << "logos::tips_req_client::receive_tips caught error in setting promise: " << e.what() << std::endl;
            }
        }
    });
}

void logos::tips_req_client::unsynced (MDB_txn * transaction_a, logos::block_hash const & head, logos::block_hash const & end)
{ // NOTE: Called from 'logos::tips_req_client::received_tips'
}

void logos::tips_req_client::received_tips (boost::system::error_code const & ec, size_t size_a)
{ // NOTE: Called from 'logos::tips_req_client::receive_tips'
}

void logos::tips_req_client::received_batch_block_tips(boost::system::error_code const &ec, size_t size_a)
{
    std::cout << "logos::tips_req_client::received_batch_block_tips: ec: " << ec << std::endl;
    LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips: ec: " << ec << std::endl;

    if(!ec) {
        uint8_t *data = connection->receive_buffer.data(); // Get it from wire.
        std::shared_ptr<BatchBlock::tips_response> tips(new BatchBlock::tips_response);
        memcpy(&(*tips),data,sizeof(BatchBlock::tips_response));

        LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips : " << *tips << std::endl;

        // This is our tips algorithm...
        // Get my tips...
        BlockHash epoch_tip = EpochBlock::getEpochBlockTip(connection->node->store);
        BlockHash micro_tip = Micro::getMicroBlockTip(connection->node->store);
        BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(connection->node->store, tips->delegate_id);

        // Get my seq numbers...
        uint32_t  epoch_seq = EpochBlock::getEpochBlockSeqNr(connection->node->store);
        uint32_t  micro_seq = Micro::getMicroBlockSeqNr(connection->node->store);
        uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(connection->node->store, tips->delegate_id);

        //  Calculate in-memory tips, and use them if they are non-zero...
        auto in_memory_epoch_tip = connection->node->_validator->in_memory_epoch_tips();
        auto in_memory_micro_tip = connection->node->_validator->in_memory_micro_tips();
        auto in_memory_bsb_tips  = connection->node->_validator->in_memory_bsb_tips();

        if(!in_memory_epoch_tip.second.is_zero() && in_memory_epoch_tip.first >= 0) {
            epoch_seq = in_memory_epoch_tip.first;
            epoch_tip = in_memory_epoch_tip.second;
        }

        if(!in_memory_micro_tip.second.is_zero() && in_memory_micro_tip.first >= 0) {
            micro_seq = in_memory_micro_tip.first;
            micro_tip = in_memory_micro_tip.second;
        }

        auto iter = in_memory_bsb_tips.find(tips->delegate_id);
        if(iter != in_memory_bsb_tips.end()) {
            bsb_seq = iter->second.first;
            bsb_tip = iter->second.second;
        }

        //LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips:: tips<1>... delegate: "  
        std::cout << "logos::tips_req_client::received_batch_block_tips:: tips<1>... delegate: "   // RGD
                  << tips->delegate_id << " "
                  << " epoch_tip: " << epoch_tip.to_string() << " tips.epoch_seq: " << tips->epoch_block_tip.to_string() << " "
                  << " micro_tip: " << micro_tip.to_string() << " tips.micro_seq: " << tips->micro_block_tip.to_string() << " "
                  << " bsb_tip: "   << bsb_tip.to_string()   << " tips.bsb_seq: "   << tips->batch_block_tip.to_string() << std::endl;

        //  Am I behind or ahead for this delegate...
        BlockHash zero = 0;

        // Send out our request for epoch and micro blocks.
        // The underlying pull code handles one of three different
        // block types (epoch/micro/bsb) and sends them to validator
        // when blocks arrive.
        if(tips->delegate_id == 0) {
            bool pull_epoch_block = ((epoch_seq == 0 and tips->epoch_block_seq_number == 0) ? true : false);
            bool pull_micro_block = ((micro_seq == 0 and tips->micro_block_seq_number == 0) ? true : false);
            // Get Epoch blocks...
            if(epoch_seq < tips->epoch_block_seq_number || pull_epoch_block) {
                // I have less sequence number than my peer, I am behind...
                if((epoch_seq == 0) && (epoch_tip == tips->epoch_block_tip)) {
                    connection->attempt->add_pull(logos::pull_info(0,0,
                             0,0,
                             tips->delegate_id,tips->epoch_block_tip,tips->epoch_block_tip,
                             zero,zero,
                             zero,zero));
                } else {
                    connection->attempt->add_pull(logos::pull_info(0,0,
                             0,0,
                             tips->delegate_id,epoch_tip,tips->epoch_block_tip,
                             zero,zero,
                             zero,zero));
                }
                LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips:: bulk_pull: delegate_id: epoch: " << tips->delegate_id << std::endl;
            } else if(epoch_seq == tips->epoch_block_seq_number) {
                    // We are in sync, continue processing...
                    LOG_DEBUG(connection->node->log) << "epoch in sync" << std::endl;
            } else {
                LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips:: epoch error... epoch_seq: " << epoch_seq << " tips.epoch_seq: " << tips->epoch_block_seq_number << std::endl;
                if (connection->node->config.logging.bulk_pull_logging ())
                {
                    LOG_INFO (connection->node->log) << "invalid tips state";
                }
            }

            // Get micro blocks...
            if(micro_seq < tips->micro_block_seq_number || pull_micro_block) {
                // I have less sequence number than my peer, I am behind...
                if((micro_seq == 0) && (micro_tip == tips->micro_block_tip)) {
                    connection->attempt->add_pull(logos::pull_info(0,0,
                             0,0,
                             tips->delegate_id,
                             zero,zero,
                             tips->micro_block_tip,tips->micro_block_tip,
                             zero,zero));
                } else {
                    connection->attempt->add_pull(logos::pull_info(0,0,
                             0,0,
                             tips->delegate_id,
                             zero,zero,
                             micro_tip,tips->micro_block_tip,
                             zero,zero));
                }
                LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips:: bulk_pull: delegate_id: micro: " << tips->delegate_id << std::endl;
            } else if(micro_seq == tips->micro_block_seq_number) {
                    // We are in sync, continue processing...
                    LOG_DEBUG(connection->node->log) << "micro in sync" << std::endl;
            } else {
                LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips:: micro error... micro_seq: " << micro_seq << " tips.micro_seq: " << tips->micro_block_seq_number << std::endl;
                if (connection->node->config.logging.bulk_pull_logging ())
                {
                    LOG_INFO (connection->node->log) << "invalid tips state";
                }
            }
        
        }

        // bsb...
        if(bsb_seq == BatchBlock::NOT_FOUND) {
                // Init, we have nothing for this delegate...
            connection->attempt->add_pull(logos::pull_info(0,0,
                         bsb_seq,tips->batch_block_seq_number,
                         tips->delegate_id,
                         zero,zero,
                         zero,zero,
                         tips->batch_block_tip,tips->batch_block_tip));
            LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips:: init bulk_pull: delegate_id: " << tips->delegate_id << " tips.batch_block_tip: " << tips->batch_block_tip.to_string() << std::endl; 
        } else if(bsb_seq < tips->batch_block_seq_number) {
            // I have less sequence number than my peer, I am behind...
            connection->attempt->add_pull(logos::pull_info(0,0,
                         bsb_seq,tips->batch_block_seq_number,
                         tips->delegate_id,
                         zero,zero,
                         zero,zero,
                         bsb_tip,tips->batch_block_tip));
            LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips:: bulk_pull: delegate_id: " << tips->delegate_id << std::endl;
        } else if(bsb_seq == tips->batch_block_seq_number) {
                // We are in sync, continue processing...
                connection->node->_validator->validate(nullptr);
                LOG_DEBUG(connection->node->log) << "in sync: delegate_id: " << tips->delegate_id << " epoch: " << epoch_seq << " theirs: " << tips->epoch_block_seq_number << " "
                          << " micro: " << micro_seq << " theirs: " << tips->micro_block_seq_number <<  " "
                          << " bsb: " << bsb_seq << " theirs: " << tips->batch_block_seq_number << std::endl;
        } else {
            LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips:: bsb error..." << std::endl
                      << " epoch_seq: " << epoch_seq << " tips.epoch_seq: " << tips->epoch_block_seq_number << std::endl
                      << " micro_seq: " << micro_seq << " tips.micro_seq: " << tips->micro_block_seq_number << std::endl
                      << " bsb_seq: "   << bsb_seq   << " tips.bsb_seq: "   << tips->batch_block_seq_number << std::endl;
            if (connection->node->config.logging.bulk_pull_logging ())
            {
                LOG_INFO (connection->node->log) << "invalid tips state";
            }
        }
        if(tips->delegate_id == (NUMBER_DELEGATES-1)) {
            try {
                promise.set_value(false); // We got everything, indicate we are ok to bootstrap.cpp...
            } catch(const std::future_error &e)
            {
                LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips caught error in setting promise: " << e.what() << std::endl;
            }
            connection->attempt->pool_connection (connection);
        }
        receive_tips_header ();
    } else {
        LOG_DEBUG(connection->node->log) << "logos::tips_req_client::received_batch_block_tips error..." << std::endl;
    }
}

void logos::tips_req_client::next (MDB_txn * transaction_a)
{
}

// NOTE Server sends tips to the client, client decides what to do
logos::tips_req_server::tips_req_server (std::shared_ptr<logos::bootstrap_server> const & connection_a, std::unique_ptr<logos::frontier_req> request_a) :
connection (connection_a),
current (request_a->start.number () - 1),
info (0, 0, 0, 0, 0, 0, 0, 0),
next_delegate(0),
request (std::move (request_a))
{
    nr_delegate = request->nr_delegate;
    LOG_DEBUG(connection->node->log) << "logos::tips_req_server::tips_req_server request->nr_delegate: " << request->nr_delegate << " nr_delegate:: " << nr_delegate << " NUMBER_DELEGATES " << NUMBER_DELEGATES << std::endl;
}

logos::tips_req_server::~tips_req_server()
{
    LOG_DEBUG(connection->node->log) << "logos::~tips_req_server:: called" << std::endl;
}

void logos::tips_req_server::skip_old ()
{
}

void logos::tips_req_server::send_next ()
{
    LOG_DEBUG(connection->node->log) << "logos::tips_req_server::send_next: " << std::endl;

    if(nr_delegate > next_delegate)
    {
       LOG_DEBUG(connection->node->log) << "logos::tips_req_server::send_next:: next_delegate: " << next_delegate << " nr_delegate: " << nr_delegate << std::endl;
       send_batch_blocks_tips();
       next_delegate++; 
    }
    else
    {
        LOG_DEBUG(connection->node->log) << "logos::tips_req_server::send_next:: send_finished" << std::endl;
        send_finished();
    }
}

void logos::tips_req_server::send_finished ()
{
    LOG_DEBUG(connection->node->log) << "logos::tips_req_server::send_finished" << std::endl;

    auto send_buffer (std::make_shared<std::vector<uint8_t>>(sizeof(BatchBlock::tips_response), uint8_t(0)));
    //auto send_buffer (std::make_shared<std::vector<uint8_t>> ());
    {
        BatchBlock::tips_response resp;
        memset(&resp,0x0,sizeof(resp));
        resp.delegate_id            = -1;
        memcpy(send_buffer->data(),(void *)&resp, sizeof(resp));
    }

    auto this_l = this;
    if (connection->node->config.logging.network_logging ())
    {
        LOG_INFO (connection->node->log) << "tips sending finished";
    }

    async_write (*connection->socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()), [this_l,send_buffer](boost::system::error_code const & ec, size_t size_a) {
        this_l->no_block_sent (ec, size_a);
    });
}

void logos::tips_req_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        LOG_DEBUG(connection->node->log) << " logos::tips_req_server::no_block_sent connection: " << connection << std::endl;
        connection->finish_request ();
    }
    else
    {
        if (connection->node->config.logging.network_logging ())
        {
            LOG_INFO (connection->node->log) << boost::str (boost::format ("Error sending tips finish: %1%") % ec.message ());
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
            LOG_INFO (connection->node->log) << boost::str (boost::format ("Error sending tips pair: %1%") % ec.message ());
        }
    }
}

void logos::tips_req_server::next ()
{
}

void logos::tips_req_server::send_batch_blocks_tips()
{
    LOG_DEBUG(connection->node->log) << "logos::tips_req_server::send_batch_blocks_tips: " << std::endl;

    auto this_l = this;

    if(nr_delegate == NUMBER_DELEGATES) {
        // NOTE Get the tips for all delegates and send them one by one for processing...
        for(int i = 0; i < nr_delegate; i++)
        {
            BatchBlock::tips_response resp;
            // Get my tips...
            BlockHash epoch_tip = EpochBlock::getEpochBlockTip(connection->node->store);
            BlockHash micro_tip = Micro::getMicroBlockTip(connection->node->store);
            BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(connection->node->store, i);
            // Get my seq numbers...
            uint32_t  epoch_seq = EpochBlock::getEpochBlockSeqNr(connection->node->store);
            uint32_t  micro_seq = Micro::getMicroBlockSeqNr(connection->node->store);
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

            Micro::dumpMicroBlockTips(connection->node->store,micro_tip);

            LOG_DEBUG(connection->node->log)
                      << "logos::tips_req_server::send_batch_blocks_tips:: tips<2>... delegate: "  << i << " "
                      << " epoch_tip: " << epoch_tip.to_string() << " "
                      << " micro_tip: " << micro_tip.to_string() << " "
                      << " bsb_tip: "   << bsb_tip.to_string()   << " ";

            // All done, write it out to the client...
            auto send_buffer1 (std::make_shared<std::vector<uint8_t>>(sizeof(BatchBlock::tips_response), uint8_t(0)));
            {
                memcpy(send_buffer1->data(),(void *)&resp, sizeof(resp));
            }

            LOG_DEBUG(connection->node->log) << "send_batch_blocks_tips this: " << this << " connection: " << connection << " this_l->connection: " << this_l->connection << std::endl;
            boost::asio::write(*connection->socket, boost::asio::buffer (send_buffer1->data (), send_buffer1->size ()),
                    boost::asio::transfer_all());

        }
    } else {
        LOG_DEBUG(connection->node->log) 
                  << "logos::tips_req_server::send_batch_blocks_tips error: " << std::endl
                  << " nr_delegate: " << nr_delegate << " NUMBER_DELEGATES " << NUMBER_DELEGATES << std::endl;
        // Log an error.
        if (this_l->connection->node->config.logging.bulk_pull_logging ())
        {
            LOG_INFO (this_l->connection->node->log) << "number of delegates does not match: server: " << NUMBER_DELEGATES << " client: " << nr_delegate;
        }
    }
}
