#include <logos/node/node.hpp>
#include <logos/node/websocket.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <chrono>

template<ConsensusType CT>
logos::websocket::message logos::websocket::block_confirm_message_builder::build (const PostCommittedBlock<CT> & block)
{
    logos::websocket::message message_l (logos::websocket::topic::confirmation);
    message_l.contents.add ("block_type", ConsensusToName(CT));

    boost::property_tree::ptree block_node;
    block.SerializeJson (block_node);
    message_l.contents.add_child ("block", block_node);
    return message_l;
}

logos::websocket::confirmation_options::confirmation_options (boost::property_tree::ptree const & options_a)
{
	include_rb = options_a.get<bool> ("include_request_block", true);
    include_mb = options_a.get<bool> ("include_micro_block", true);
	include_eb = options_a.get<bool> ("include_epoch_block", true);

	auto accounts_l (options_a.get_child_optional ("accounts"));
	if (accounts_l)
	{
		for (auto account_l : *accounts_l)
		{
			logos::account result_l (0);
			if (!result_l.decode_account (account_l.second.data ()))
			{
				// Do not insert the given raw data to keep old prefix support
				accounts.insert (result_l.to_account ());
			}
			else
			{
                Log log;
				LOG_WARN(log) << "Websocket: invalid account provided for filtering blocks: "
                               << account_l.second.data ();
			}
		}
	}
}

bool logos::websocket::confirmation_options::interested(const ApprovedRB & block)
{
    //TODO account will supported later
    return include_rb;
}
bool logos::websocket::confirmation_options::interested(const ApprovedMB & block)
{
    return include_mb;
}
bool logos::websocket::confirmation_options::interested(const ApprovedEB & block)
{
    return include_eb;
}


logos::websocket::session::session (logos::websocket::listener & listener_a, socket_type socket_a) :
ws_listener (listener_a), ws (std::move (socket_a)), strand (ws.get_executor ())
{
	ws.text (true);
	LOG_INFO(log) << "Websocket: session started";
}

logos::websocket::session::~session ()
{
    LOG_TRACE(log) << "Websocket::session::dtor";

	{
		std::unique_lock<std::mutex> lk (subscriptions_mutex);
		for (auto & subscription : subscriptions)
		{
			ws_listener.decrease_subscriber_count (subscription.first);
		}
	}
}

void logos::websocket::session::handshake ()
{
    LOG_TRACE(log) << "Websocket::session::handshake";

	auto this_l (shared_from_this ());
	ws.async_accept ([this_l](boost::system::error_code const & ec) {
		if (!ec)
		{
			// Start reading incoming messages
			this_l->read ();
		}
		else
		{
			LOG_WARN(this_l->log) << "Websocket: handshake failed: " << ec.message ();
		}
	});
}

void logos::websocket::session::close ()
{
	LOG_INFO(log) << "Websocket: session closing";

	auto this_l (shared_from_this ());
	// clang-format off
	boost::asio::dispatch (strand,
	[this_l]() {
		boost::beast::websocket::close_reason reason;
		reason.code = boost::beast::websocket::close_code::normal;
		reason.reason = "Shutting down";
		boost::system::error_code ec_ignore;
		this_l->ws.close (reason, ec_ignore);
	});
	// clang-format on
}

void logos::websocket::session::write (logos::websocket::message message_a)
{
    LOG_TRACE(log) << "Websocket::session::write";

	// clang-format off
	std::unique_lock<std::mutex> lk (subscriptions_mutex);
	auto subscription (subscriptions.find (message_a.topic));
	if (message_a.topic == logos::websocket::topic::ack ||
        (subscription != subscriptions.end () && !subscription->second->should_filter (message_a)))
	{
		lk.unlock ();
		auto this_l (shared_from_this ());
		boost::asio::post (strand,
		[message_a, this_l]() {
			bool write_in_progress = !this_l->send_queue.empty ();
			this_l->send_queue.emplace_back (message_a);
			if (!write_in_progress)
			{
				this_l->write_queued_messages ();
			}
		});
	}
	// clang-format on
}

void logos::websocket::session::write_queued_messages ()
{
    LOG_TRACE(log) << "Websocket::session::write_queued_messages";

	auto msg (send_queue.front ());
	auto msg_str (msg.to_string ());
	auto this_l (shared_from_this ());

	// clang-format off
	ws.async_write (boost::asio::buffer (msg_str->data (), msg_str->size ()),
	boost::asio::bind_executor (strand,
	[msg_str, this_l](boost::system::error_code ec, std::size_t bytes_transferred) {
		this_l->send_queue.pop_front ();
		if (!ec)
		{
			if (!this_l->send_queue.empty ())
			{
				this_l->write_queued_messages ();
			}
		}
	}));
	// clang-format on
}

void logos::websocket::session::read ()
{
    LOG_TRACE(log) << "Websocket::session::read";

	auto this_l (shared_from_this ());

	// clang-format off
	boost::asio::post (strand, [this_l]() {
		this_l->ws.async_read (this_l->read_buffer,
		boost::asio::bind_executor (this_l->strand,
		[this_l](boost::system::error_code ec, std::size_t bytes_transferred) {
			if (!ec)
			{
				std::stringstream os;
				os << beast_buffers (this_l->read_buffer.data ());
				std::string incoming_message = os.str ();
                LOG_TRACE(this_l->log) << "Websocket::session::read: "
                                       << incoming_message;
				// Prepare next read by clearing the multibuffer
				this_l->read_buffer.consume (this_l->read_buffer.size ());

				boost::property_tree::ptree tree_msg;
				try
				{
					boost::property_tree::read_json (os, tree_msg);
					this_l->handle_message (tree_msg);
					this_l->read ();
				}
				catch (boost::property_tree::json_parser::json_parser_error const & ex)
				{
                    LOG_WARN(this_l->log) << "Websocket: json parsing failed: " << ex.what ();
				}
			}
			else if (ec != boost::asio::error::eof)
			{
				LOG_WARN(this_l->log) << "Websocket: read failed: " << ec.message ();
			}
		}));
	});
	// clang-format on
}

namespace
{
logos::websocket::topic to_topic (std::string const & topic_a)
{
	logos::websocket::topic topic = logos::websocket::topic::invalid;
	if (topic_a == "confirmation")
	{
		topic = logos::websocket::topic::confirmation;
	}
	else if (topic_a == "ack")
	{
		topic = logos::websocket::topic::ack;
	}

	return topic;
}

std::string from_topic (logos::websocket::topic topic_a)
{
	std::string topic = "invalid";
	if (topic_a == logos::websocket::topic::confirmation)
	{
		topic = "confirmation";
	}
	else if (topic_a == logos::websocket::topic::ack)
	{
		topic = "ack";
	}
	return topic;
}
}

void logos::websocket::session::send_ack (std::string action_a, std::string id_a)
{
    LOG_TRACE(log) << "Websocket::session::send_ack";
	auto milli_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
	logos::websocket::message msg (logos::websocket::topic::ack);
	boost::property_tree::ptree & message_l = msg.contents;
	message_l.add ("ack", action_a);
	message_l.add ("time", std::to_string (milli_since_epoch));
	if (!id_a.empty ())
	{
		message_l.add ("id", id_a);
	}
	write (msg);
}

void logos::websocket::session::handle_message (boost::property_tree::ptree const & message_a)
{
    LOG_DEBUG(log) << "Websocket::session::handle_message";
	std::string action (message_a.get<std::string> ("action", ""));
	auto topic_l (to_topic (message_a.get<std::string> ("topic", "")));
	auto ack_l (message_a.get<bool> ("ack", false));
	auto id_l (message_a.get<std::string> ("id", ""));
	auto action_succeeded (false);
	if (action == "subscribe" && topic_l != logos::websocket::topic::invalid)
	{
		auto options_text_l (message_a.get_child_optional ("options"));
		std::lock_guard<std::mutex> lk (subscriptions_mutex);
		std::unique_ptr<logos::websocket::options> options_l{ nullptr };
		if (options_text_l && topic_l == logos::websocket::topic::confirmation)
		{
			options_l = std::make_unique<logos::websocket::confirmation_options> (options_text_l.get ());
		}
		else
		{
			options_l = std::make_unique<logos::websocket::options> ();
		}
		auto existing (subscriptions.find (topic_l));
		if (existing != subscriptions.end ())
		{
			existing->second = std::move (options_l);
			LOG_INFO(log) << "Websocket: updated subscription to topic: " << from_topic (topic_l);
		}
		else
		{
			subscriptions.insert (std::make_pair (topic_l, std::move (options_l)));
			LOG_INFO(log) << "Websocket: new subscription to topic: "<< from_topic (topic_l);
			ws_listener.increase_subscriber_count (topic_l);
		}
		action_succeeded = true;
	}
	else if (action == "unsubscribe" && topic_l != logos::websocket::topic::invalid)
	{
		std::lock_guard<std::mutex> lk (subscriptions_mutex);
		if (subscriptions.erase (topic_l))
		{
			LOG_INFO(log) << "Websocket: removed subscription to topic: " << from_topic (topic_l);
			ws_listener.decrease_subscriber_count (topic_l);
		}
		action_succeeded = true;
	}
	if (ack_l && action_succeeded)
	{
		send_ack (action, id_l);
	}
}

void logos::websocket::listener::stop ()
{
    LOG_TRACE(log) << "Websocket::listener::stop";

	stopped = true;
	acceptor.close ();

	std::lock_guard<std::mutex> lk (sessions_mutex);
	for (auto & weak_session : sessions)
	{
		auto session_ptr (weak_session.lock ());
		if (session_ptr)
		{
			session_ptr->close ();
		}
	}
	sessions.clear ();
}

logos::websocket::listener::listener (boost::asio::io_service & service, std::string & local_address) :
acceptor (service),
socket (service)
{
    boost::asio::ip::tcp::endpoint endpoint_a(boost::asio::ip::address::from_string(local_address), listener_port);
	try
	{
		acceptor.open (endpoint_a.protocol ());
		acceptor.set_option (boost::asio::socket_base::reuse_address (true));
		acceptor.bind (endpoint_a);
		acceptor.listen (boost::asio::socket_base::max_listen_connections);
        LOG_DEBUG(log) << "Websocket: listener constructed";
    }
	catch (std::exception const & ex)
	{
		LOG_WARN(log) << "Websocket: listener ctor, listen failed: " << ex.what ();
	}
}

void logos::websocket::listener::run ()
{
    LOG_DEBUG(log) << "Websocket: listener started";
	if (acceptor.is_open ())
	{
		accept ();
	}
}

void logos::websocket::listener::accept ()
{
    LOG_DEBUG(log) << "Websocket::listener::accept";
	auto this_l (shared_from_this ());
	acceptor.async_accept (socket,
	[this_l](boost::system::error_code const & ec) {
		this_l->on_accept (ec);
	});
}

void logos::websocket::listener::on_accept (boost::system::error_code ec)
{
    LOG_TRACE(log) << "Websocket::listener::on_accept";

	if (ec)
	{
		LOG_WARN(log) << "Websocket: accept failed: " << ec.message ();
	}
	else
	{
		// Create the session and initiate websocket handshake
		auto session (std::make_shared<logos::websocket::session> (*this, std::move (socket)));
		sessions_mutex.lock ();
		sessions.push_back (session);
		// Clean up expired sessions
		sessions.erase (std::remove_if (sessions.begin (), sessions.end (), [](auto & elem) { return elem.expired (); }), sessions.end ());
		sessions_mutex.unlock ();
		session->handshake ();
	}

	if (!stopped)
	{
		accept ();
	}
}

template <ConsensusType CT>
void logos::websocket::listener::broadcast_confirmation (const PostCommittedBlock<CT> & block)
{
    LOG_TRACE(log) << "websocket::listener::broadcast_confirmation: " << block.ToJson();

	logos::websocket::block_confirm_message_builder builder;

	std::lock_guard<std::mutex> lk (sessions_mutex);
	for (auto & weak_session : sessions)
	{
		auto session_ptr (weak_session.lock ());
		if (session_ptr)
		{
			auto subscription (session_ptr->subscriptions.find (logos::websocket::topic::confirmation));
			if (subscription != session_ptr->subscriptions.end ())
			{
				logos::websocket::confirmation_options default_options;
				auto conf_options (dynamic_cast<logos::websocket::confirmation_options *> (subscription->second.get ()));
				if (conf_options == nullptr)
				{
					conf_options = &default_options;
				}
                if(conf_options->interested(block))
                {
                    session_ptr->write (builder.build(block));
                }
			}
		}
	}
}

void logos::websocket::listener::broadcast (logos::websocket::message message_a)
{
    LOG_TRACE(log) << "Websocket::listener::broadcast";

	std::lock_guard<std::mutex> lk (sessions_mutex);
	for (auto & weak_session : sessions)
	{
		auto session_ptr (weak_session.lock ());
		if (session_ptr)
		{
			session_ptr->write (message_a);
		}
	}
}

void logos::websocket::listener::increase_subscriber_count (logos::websocket::topic const & topic_a)
{
    LOG_TRACE(log) << "Websocket::listener::increase_subscriber_count";

	topic_subscriber_count[static_cast<std::size_t> (topic_a)] += 1;
}

void logos::websocket::listener::decrease_subscriber_count (logos::websocket::topic const & topic_a)
{
    LOG_TRACE(log) << "Websocket::listener::decrease_subscriber_count";

	auto & count (topic_subscriber_count[static_cast<std::size_t> (topic_a)]);
	assert (count > 0);
	count -= 1;
}

std::shared_ptr<std::string> logos::websocket::message::to_string () const
{
	std::ostringstream ostream;
	boost::property_tree::write_json (ostream, contents);
	ostream.flush ();
	return std::make_shared<std::string> (ostream.str ());
}

namespace logos_global
{
    template <ConsensusType CT>
    void OnNewBlock(const PostCommittedBlock<CT> & block)
    {
        auto n = GetNode();
        if(n != nullptr)
        {
            n->websocket_server->broadcast_confirmation(block);
        }
     }

    template void OnNewBlock<ConsensusType::Request>(const ApprovedRB & block);
    template void OnNewBlock<ConsensusType::MicroBlock>(const ApprovedMB & block);
    template void OnNewBlock<ConsensusType::Epoch>(const ApprovedEB & block);
}
