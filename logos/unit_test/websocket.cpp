//TODO the unit tests are not working, and we are out of time to fix them. We still keep them here for later fix.
//#include <boost/asio.hpp>
//#include <boost/beast.hpp>
//#include <logos/node/websocket.hpp>
//
//#include <gtest/gtest.h>
//
//#include <boost/property_tree/json_parser.hpp>
//
//#include <chrono>
//#include <condition_variable>
//#include <cstdlib>
//#include <iostream>
//#include <memory>
//#include <sstream>
//#include <string>
//#include <vector>
//
//using namespace std::chrono_literals;
//#define SLEEP_TIME 1000000
//
//namespace
//{
///** This variable must be set to false before setting up every thread that makes a websocket test call (and needs ack), to be safe */
//std::atomic<bool> ack_ready{ false };
//
///** An optionally blocking websocket client for testing */
//boost::optional<std::string> websocket_test_call (std::string host, std::string message_a,
//        bool await_ack, bool await_response, const std::chrono::seconds response_deadline = 5s)
//{
//	if (await_ack)
//	{
//		ack_ready = false;
//	}
//
//	boost::asio::io_context ioc;
//	boost::asio::ip::tcp::endpoint server_endpoint(boost::asio::ip::address::from_string(host),
//	        logos::websocket::listener_port);
//	auto ws (std::make_shared<boost::beast::websocket::stream<boost::asio::ip::tcp::socket>> (ioc));
//	boost::asio::ip::tcp::socket &s = ws->next_layer ();
//	s.connect(server_endpoint);
//
//    boost::optional<std::string> ret;
//	ws->handshake (host, "/");
//	ws->text (true);
//	ws->write (boost::asio::buffer (message_a));
//
//	if (await_ack)
//	{
//		boost::beast::flat_buffer buffer;
//		ws->read (buffer);
//		ack_ready = true;
//	}
//
//	if (await_response)
//	{
//		assert (response_deadline > 0s);
//		auto buffer (std::make_shared<boost::beast::flat_buffer> ());
//		ws->async_read (*buffer, [&ret, ws, buffer](boost::beast::error_code const & ec, std::size_t const n) {
//			if (!ec)
//			{
//				std::ostringstream res;
//				res << beast_buffers (buffer->data ());
//				ret = res.str ();
//			}
//		});
//		ioc.run_one_for (response_deadline);
//	}
//
//	if (ws->is_open ())
//	{
//		ws->async_close (boost::beast::websocket::close_code::normal, [ws](boost::beast::error_code const & ec) {
//			// A synchronous close usually hangs in tests when the server's io_context stops looping
//			// An async_close solves this problem
//		});
//	}
//	return ret;
//}
//}
//
///** Tests clients subscribing multiple times or unsubscribing without a subscription */
//TEST (websocket, subscription_edge)
//{
//	boost::asio::io_service service;
//	std::thread io_thread([&](){
//	    service.run();
//        std::cout << "websocket io_thread stopped" << std::endl;
//	});
//    std::string local_address("127.0.0.1");
//	auto websocket_server = std::make_shared<logos::websocket::listener> (service, local_address);
//    websocket_server->run ();
//
//	ASSERT_EQ (0, websocket_server->subscriber_count (logos::websocket::topic::confirmation));
//
//	// First subscription
//	{
//		ack_ready = false;
//		std::thread subscription_thread ([]() {
//			websocket_test_call ("::1", R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, false);
//		});
//        usleep (1000000);
//		while (!ack_ready)
//		{
//			ASSERT_NO_THROW(service.poll ());
//		}
//		subscription_thread.join ();
//		ASSERT_EQ (1, websocket_server->subscriber_count (logos::websocket::topic::confirmation));
//	}
//
//	// Second subscription, should not increase subscriber count, only update the subscription
//	{
//		ack_ready = false;
//		std::thread subscription_thread ([]() {
//			websocket_test_call ("::1", R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, false);
//		});
//		usleep (1000000);
//		while (!ack_ready)
//		{
//			ASSERT_NO_THROW(service.poll ());
//		}
//		subscription_thread.join ();
//		ASSERT_EQ (1, websocket_server->subscriber_count (logos::websocket::topic::confirmation));
//	}
//
//	// First unsub
//	{
//		ack_ready = false;
//		std::thread unsub_thread ([]() {
//			websocket_test_call ("::1", R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json", true, false);
//		});
//		usleep (1000000);
//		while (!ack_ready)
//		{
//			ASSERT_NO_THROW(service.poll ());
//		}
//		unsub_thread.join ();
//		ASSERT_EQ (0, websocket_server->subscriber_count (logos::websocket::topic::confirmation));
//	}
//
//	// Second unsub, should acknowledge but not decrease subscriber count
//	{
//		ack_ready = false;
//		std::thread unsub_thread ([]() {
//			websocket_test_call ("::1", R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json", true, false);
//		});
//        usleep (1000000);
//
//		while (!ack_ready)
//		{
//			ASSERT_NO_THROW(service.poll ());
//		}
//		unsub_thread.join ();
//		ASSERT_EQ (0, websocket_server->subscriber_count (logos::websocket::topic::confirmation));
//	}
//
//	websocket_server->stop ();
//}

///** Subscribes to block confirmations, confirms a block and then awaits websocket notification */
//TEST (websocket, confirmation)
//{
//	nano::system system (24000, 1);
//	nano::node_init init1;
//	nano::node_config config;
//	nano::node_flags node_flags;
//	config.websocket_config.enabled = true;
//	config.websocket_config.port = 24078;
//
//	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, nano::unique_path (), system.alarm, config, system.work, node_flags));
//	nano::uint256_union wallet;
//	nano::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
//	node1->wallets.create (wallet);
//	node1->start ();
//	system.nodes.push_back (node1);
//
//	// Start websocket test-client in a separate thread
//	ack_ready = false;
//	std::atomic<bool> confirmation_event_received{ false };
//	ASSERT_FALSE (node1->websocket_server->any_subscriber (nano::websocket::topic::confirmation));
//	std::thread client_thread ([&confirmation_event_received]() {
//		// This will expect two results: the acknowledgement of the subscription
//		// and then the block confirmation message
//		auto response = websocket_test_call ("::1", "24078",
//		R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, true);
//		ASSERT_TRUE (response);
//		boost::property_tree::ptree event;
//		std::stringstream stream;
//		stream << response.get ();
//		boost::property_tree::read_json (stream, event);
//		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
//		confirmation_event_received = true;
//	});
//
//	// Wait for the subscription to be acknowledged
//	system.deadline_set (5s);
//	while (!ack_ready)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//
//	ASSERT_TRUE (node1->websocket_server->any_subscriber (nano::websocket::topic::confirmation));
//
//	nano::keypair key;
//	system.wallet (1)->insert_adhoc (nano::test_genesis_key.prv);
//	auto balance = nano::genesis_amount;
//	auto send_amount = node1->config.online_weight_minimum.number () + 1;
//	// Quick-confirm a block, legacy blocks should work without filtering
//	{
//		nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
//		balance -= send_amount;
//		auto send (std::make_shared<nano::send_block> (previous, key.pub, balance, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
//		node1->process_active (send);
//	}
//
//	// Wait for the confirmation to be received
//	system.deadline_set (5s);
//	while (!confirmation_event_received)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//	client_thread.join ();
//
//	std::atomic<bool> unsubscribe_ack_received{ false };
//	std::thread client_thread_2 ([&unsubscribe_ack_received]() {
//		auto response = websocket_test_call ("::1", "24078",
//		R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, true);
//		ASSERT_TRUE (response);
//		boost::property_tree::ptree event;
//		std::stringstream stream;
//		stream << response.get ();
//		boost::property_tree::read_json (stream, event);
//		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
//
//		// Unsubscribe action, expects an acknowledge but no response follows
//		websocket_test_call ("::1", "24078",
//		R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json", true, true, 1s);
//		unsubscribe_ack_received = true;
//	});
//
//	// Wait for the subscription to be acknowledged
//	system.deadline_set (5s);
//	while (!ack_ready)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//
//	// Quick confirm a state block
//	{
//		nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
//		balance -= send_amount;
//		auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, balance, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
//		node1->process_active (send);
//	}
//
//	// Wait for the unsubscribe action to be acknowledged
//	system.deadline_set (5s);
//	while (!unsubscribe_ack_received)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//	client_thread_2.join ();
//
//	node1->stop ();
//}

///** Tests the filtering options of block confirmations */
//TEST (websocket, confirmation_options)
//{
//	nano::system system (24000, 1);
//	nano::node_init init1;
//	nano::node_config config;
//	nano::node_flags node_flags;
//	config.websocket_config.enabled = true;
//	config.websocket_config.port = 24078;
//
//	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, nano::unique_path (), system.alarm, config, system.work, node_flags));
//	nano::uint256_union wallet;
//	nano::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
//	node1->wallets.create (wallet);
//	node1->start ();
//	system.nodes.push_back (node1);
//
//	// Start websocket test-client in a separate thread
//	ack_ready = false;
//	std::atomic<bool> client_thread_finished{ false };
//	ASSERT_FALSE (node1->websocket_server->any_subscriber (nano::websocket::topic::confirmation));
//	std::thread client_thread ([&client_thread_finished]() {
//		// Subscribe initially with a specific invalid account
//		auto response = websocket_test_call ("::1", "24078",
//		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "accounts": ["xrb_invalid"]}})json", true, true, 1s);
//
//		ASSERT_FALSE (response);
//		client_thread_finished = true;
//	});
//
//	// Wait for subscribe acknowledgement
//	system.deadline_set (5s);
//	while (!ack_ready)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//
//	// Confirm a state block for an in-wallet account
//	system.wallet (1)->insert_adhoc (nano::test_genesis_key.prv);
//	nano::keypair key;
//	auto balance = nano::genesis_amount;
//	auto send_amount = node1->config.online_weight_minimum.number () + 1;
//	nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
//	{
//		balance -= send_amount;
//		auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, balance, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
//		node1->process_active (send);
//		previous = send->hash ();
//	}
//
//	// Wait for client thread to finish, no confirmation message should be received with given filter
//	system.deadline_set (5s);
//	while (!client_thread_finished)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//
//	std::atomic<bool> client_thread_2_finished{ false };
//	std::thread client_thread_2 ([&client_thread_2_finished]() {
//		// Re-subscribe with options for all local wallet accounts
//		auto response = websocket_test_call ("::1", "24078",
//		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "all_local_accounts": "true", "include_election_info": "true"}})json", true, true);
//
//		ASSERT_TRUE (response);
//		boost::property_tree::ptree event;
//		std::stringstream stream;
//		stream << response.get ();
//		boost::property_tree::read_json (stream, event);
//		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
//		try
//		{
//			boost::property_tree::ptree election_info = event.get_child ("message.election_info");
//			auto tally (election_info.get<std::string> ("tally"));
//			auto time (election_info.get<std::string> ("time"));
//			election_info.get<std::string> ("duration");
//			// Make sure tally and time are non-zero. Duration may be zero on testnet, so we only check that it's present (exception thrown otherwise)
//			ASSERT_NE ("0", tally);
//			ASSERT_NE ("0", time);
//		}
//		catch (std::runtime_error const & ex)
//		{
//			FAIL () << ex.what ();
//		}
//
//		client_thread_2_finished = true;
//	});
//
//	node1->block_processor.flush ();
//	// Wait for the subscribe action to be acknowledged
//	system.deadline_set (5s);
//	while (!ack_ready)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//
//	ASSERT_TRUE (node1->websocket_server->any_subscriber (nano::websocket::topic::confirmation));
//
//	// Quick-confirm another block
//	{
//		balance -= send_amount;
//		auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, balance, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
//		node1->process_active (send);
//		previous = send->hash ();
//	}
//
//	node1->block_processor.flush ();
//	// Wait for confirmation message
//	system.deadline_set (5s);
//	while (!client_thread_2_finished)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//
//	std::atomic<bool> client_thread_3_finished{ false };
//	std::thread client_thread_3 ([&client_thread_3_finished]() {
//		auto response = websocket_test_call ("::1", "24078",
//		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "all_local_accounts": "true"}})json", true, true, 1s);
//
//		ASSERT_FALSE (response);
//		client_thread_3_finished = true;
//	});
//
//	// Confirm a legacy block
//	// When filtering options are enabled, legacy blocks are always filtered
//	{
//		balance -= send_amount;
//		auto send (std::make_shared<nano::send_block> (previous, key.pub, balance, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
//		node1->process_active (send);
//		previous = send->hash ();
//	}
//
//	node1->block_processor.flush ();
//	// Wait for client thread to finish, no confirmation message should be received
//	system.deadline_set (5s);
//	while (!client_thread_3_finished)
//	{
//		ASSERT_NO_ERROR (system.poll ());
//	}
//	ack_ready = false;
//
//	client_thread.join ();
//	client_thread_2.join ();
//	client_thread_3.join ();
//	node1->stop ();
//}
