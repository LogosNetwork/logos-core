#include <gtest/gtest.h>

#include <rai/qt/qt.hpp>
#include <rai/node/testing.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <thread>
#include <QTest>

extern QApplication * test_application;
rai_qt::eventloop_processor processor;

TEST (wallet, construction)
{
    rai::system system (24000, 1);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
	auto key (wallet_l->deterministic_insert ());
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], wallet_l, key));
	wallet->start ();
    ASSERT_EQ (key.to_account_split (), wallet->self.account_text->text ().toStdString ());
    ASSERT_EQ (1, wallet->accounts.model->rowCount ());
    auto item1 (wallet->accounts.model->item (0, 1));
    ASSERT_EQ (key.to_account (), item1->text ().toStdString ());
}

TEST (wallet, status)
{
    rai::system system (24000, 1);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
	wallet_l->insert_adhoc (key.prv);
	auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], wallet_l, key.pub));
	wallet->start ();
	ASSERT_EQ ("Status: Disconnected, Block: 1", wallet->status->text ().toStdString ());
	system.nodes [0]->peers.insert (rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000), 0);
	ASSERT_NE ("Status: Synchronizing", wallet->status->text ().toStdString ());
	auto iterations (0);
	while (wallet->status->text ().toStdString () != "Status: Synchronizing")
	{
		test_application->processEvents ();
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	system.nodes [0]->peers.purge_list (std::chrono::system_clock::now () + std::chrono::seconds (5));
	while (wallet->status->text ().toStdString () == "Status: Synchronizing")
	{
		test_application->processEvents ();
	}
	ASSERT_EQ ("Status: Disconnected", wallet->status->text ().toStdString ());
}

TEST (wallet, startup_balance)
{
    rai::system system (24000, 1);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
	wallet_l->insert_adhoc (key.prv);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], wallet_l, key.pub));
	wallet->start ();
	ASSERT_EQ ("Balance (XRB): 0", wallet->self.balance_label->text().toStdString ());
}

TEST (wallet, select_account)
{
    rai::system system (24000, 1);
	auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
	rai::public_key key1 (wallet_l->deterministic_insert ());
	rai::public_key key2 (wallet_l->deterministic_insert ());
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], wallet_l, key1));
	wallet->start ();
	ASSERT_EQ (key1, wallet->account);
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet->accounts_button, Qt::LeftButton);
	wallet->accounts.view->selectionModel ()->setCurrentIndex (wallet->accounts.model->index (0, 0), QItemSelectionModel::SelectionFlag::Select);
	QTest::mouseClick (wallet->accounts.use_account, Qt::LeftButton);
	auto key3 (wallet->account);
	wallet->accounts.view->selectionModel ()->setCurrentIndex (wallet->accounts.model->index (1, 0), QItemSelectionModel::SelectionFlag::Select);
	QTest::mouseClick (wallet->accounts.use_account, Qt::LeftButton);
	auto key4 (wallet->account);
	ASSERT_NE (key3, key4);
}

TEST (wallet, main)
{
    rai::system system (24000, 1);
    auto wallet_l (system.nodes [0]->wallets.create (rai::uint256_union ()));
    rai::keypair key;
	wallet_l->insert_adhoc (key.prv);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], wallet_l, key.pub));
	wallet->start ();
    ASSERT_EQ (wallet->entry_window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->send_blocks, Qt::LeftButton);
    ASSERT_EQ (wallet->send_blocks_window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->send_blocks_back, Qt::LeftButton);
    QTest::mouseClick (wallet->settings_button, Qt::LeftButton);
    ASSERT_EQ (wallet->settings.window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->settings.back, Qt::LeftButton);
    ASSERT_EQ (wallet->entry_window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
    ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->advanced.show_ledger, Qt::LeftButton);
    ASSERT_EQ (wallet->advanced.ledger_window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->advanced.ledger_back, Qt::LeftButton);
    ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->advanced.show_peers, Qt::LeftButton);
    ASSERT_EQ (wallet->advanced.peers_window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->advanced.peers_back, Qt::LeftButton);
    ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->advanced.back, Qt::LeftButton);
    ASSERT_EQ (wallet->entry_window, wallet->main_stack->currentWidget ());
}

TEST (wallet, password_change)
{
    rai::system system (24000, 1);
	rai::account account;
	system.wallet (0)->insert_adhoc (rai::keypair ().prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
    QTest::mouseClick (wallet->settings_button, Qt::LeftButton);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		rai::raw_key password1;
		rai::raw_key password2;
		system.wallet (0)->store.derive_key (password1, transaction, "1");
		system.wallet (0)->store.password.value (password2);
		ASSERT_NE (password1, password2);
	}
    QTest::keyClicks (wallet->settings.new_password, "1");
    QTest::keyClicks (wallet->settings.retype_password, "1");
    QTest::mouseClick (wallet->settings.change, Qt::LeftButton);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		rai::raw_key password1;
		rai::raw_key password2;
		system.wallet (0)->store.derive_key (password1, transaction, "1");
		system.wallet (0)->store.password.value (password2);
		ASSERT_EQ (password1, password2);
	}
    ASSERT_EQ ("", wallet->settings.new_password->text ());
    ASSERT_EQ ("", wallet->settings.retype_password->text ());
}

TEST (client, password_nochange)
{
    rai::system system (24000, 1);
	rai::account account;
	system.wallet (0)->insert_adhoc (rai::keypair ().prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
    QTest::mouseClick (wallet->settings_button, Qt::LeftButton);
	auto iterations (0);
	rai::raw_key password;
	password.data.clear ();
	while (password.data == 0)
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
		system.wallet (0)->store.password.value (password);
	}
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		rai::raw_key password1;
		system.wallet (0)->store.derive_key (password1, transaction, "");
		rai::raw_key password2;
		system.wallet (0)->store.password.value (password2);
		ASSERT_EQ (password1, password2);
	}
    QTest::keyClicks (wallet->settings.new_password, "1");
    QTest::keyClicks (wallet->settings.retype_password, "2");
    QTest::mouseClick (wallet->settings.change, Qt::LeftButton);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		rai::raw_key password1;
		system.wallet (0)->store.derive_key (password1, transaction, "");
		rai::raw_key password2;
		system.wallet (0)->store.password.value (password2);
		ASSERT_EQ (password1, password2);
	}
    ASSERT_EQ ("1", wallet->settings.new_password->text ());
    ASSERT_EQ ("", wallet->settings.retype_password->text ());
}

TEST (wallet, enter_password)
{
    rai::system system (24000, 2);
	rai::account account;
	system.wallet (0)->insert_adhoc (rai::keypair ().prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
    ASSERT_NE (-1, wallet->settings.layout->indexOf (wallet->settings.password));
    ASSERT_NE (-1, wallet->settings.lock_layout->indexOf (wallet->settings.unlock));
    ASSERT_NE (-1, wallet->settings.lock_layout->indexOf (wallet->settings.lock));
    ASSERT_NE (-1, wallet->settings.layout->indexOf (wallet->settings.back));
    QTest::mouseClick (wallet->settings.unlock, Qt::LeftButton);
	test_application->processEvents();
    ASSERT_EQ ("Status: Wallet password empty", wallet->status->text ());
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		ASSERT_FALSE (system.wallet (0)->store.rekey (transaction, "abc"));
	}
    QTest::mouseClick (wallet->settings_button, Qt::LeftButton);
    QTest::keyClicks (wallet->settings.new_password, "a");
    QTest::mouseClick (wallet->settings.unlock, Qt::LeftButton);
	test_application->processEvents();
    ASSERT_EQ ("Status: Wallet locked", wallet->status->text ());
    wallet->settings.new_password->setText ("");
    QTest::keyClicks (wallet->settings.password, "abc");
    QTest::mouseClick (wallet->settings.unlock, Qt::LeftButton);
	test_application->processEvents();
	auto status (wallet->status->text ());
    ASSERT_EQ ("Status: Running", status);
    ASSERT_EQ ("", wallet->settings.password->text ());
}

TEST (wallet, send)
{
    rai::system system (24000, 2);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::public_key key1 (system.wallet (1)->insert_adhoc (rai::keypair ().prv));
	auto account (rai::test_genesis_key.pub);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
    QTest::mouseClick (wallet->send_blocks, Qt::LeftButton);
    QTest::keyClicks (wallet->send_account, key1.to_account ().c_str ());
    QTest::keyClicks (wallet->send_count, "2");
    QTest::mouseClick (wallet->send_blocks_send, Qt::LeftButton);
	auto iterations1 (0);
    while (wallet->node.balance (key1).is_zero ())
    {
        system.poll ();
		++iterations1;
		ASSERT_LT (iterations1, 200);
    }
	rai::uint128_t amount (wallet->node.balance (key1));
    ASSERT_EQ (2 * wallet->rendering_ratio, amount);
	QTest::mouseClick (wallet->send_blocks_back, Qt::LeftButton);
    QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet->advanced.show_ledger, Qt::LeftButton);
	QTest::mouseClick (wallet->advanced.ledger_refresh, Qt::LeftButton);
	ASSERT_EQ (2, wallet->advanced.ledger_model->rowCount ());
	ASSERT_EQ (3, wallet->advanced.ledger_model->columnCount ());
	auto item (wallet->advanced.ledger_model->itemFromIndex (wallet->advanced.ledger_model->index (1, 1)));
	ASSERT_EQ ("2", item->text ().toStdString ());
}

TEST (wallet, send_locked)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key1;
	system.wallet (0)->enter_password ("0");
	auto account (rai::test_genesis_key.pub);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
    QTest::mouseClick (wallet->send_blocks, Qt::LeftButton);
    QTest::keyClicks (wallet->send_account, key1.pub.to_account ().c_str ());
    QTest::keyClicks (wallet->send_count, "2");
    QTest::mouseClick (wallet->send_blocks_send, Qt::LeftButton);
	auto iterations1 (0);
    while (!wallet->send_blocks_send->isEnabled ())
    {
		test_application->processEvents ();
        system.poll ();
		++iterations1;
		ASSERT_LT (iterations1, 200);
    }
}

TEST (wallet, process_block)
{
    rai::system system (24000, 1);
	rai::account account;
	rai::block_hash latest (system.nodes [0]->latest (rai::genesis_account));
	system.wallet (0)->insert_adhoc (rai::keypair ().prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
    ASSERT_EQ ("Process", wallet->block_entry.process->text ());
    ASSERT_EQ ("Back", wallet->block_entry.back->text ());
    rai::keypair key1;
    ASSERT_EQ (wallet->entry_window, wallet->main_stack->currentWidget ());
    QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
    QTest::mouseClick (wallet->advanced.enter_block, Qt::LeftButton);
    ASSERT_EQ (wallet->block_entry.window, wallet->main_stack->currentWidget ());
    rai::send_block send (latest, key1.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (latest));
    std::string previous;
    send.hashables.previous.encode_hex (previous);
    std::string balance;
    send.hashables.balance.encode_hex (balance);
    std::string signature;
    send.signature.encode_hex (signature);
    auto block_json (boost::str (boost::format ("{\"type\": \"send\", \"previous\": \"%1%\", \"balance\": \"%2%\", \"destination\": \"%3%\", \"work\": \"%4%\", \"signature\": \"%5%\"}") % previous % balance % send.hashables.destination.to_account () % rai::to_string_hex (send.work) % signature));
    QTest::keyClicks (wallet->block_entry.block, block_json.c_str ());
    QTest::mouseClick (wallet->block_entry.process, Qt::LeftButton);
    ASSERT_EQ (send.hash (), system.nodes [0]->latest (rai::genesis_account));
    QTest::mouseClick(wallet->block_entry.back, Qt::LeftButton);
    ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
}

TEST (wallet, create_send)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto account (rai::test_genesis_key.pub);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
	wallet->client_window->show ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet->advanced.create_block, Qt::LeftButton);
	QTest::mouseClick (wallet->block_creation.send, Qt::LeftButton);
	QTest::keyClicks (wallet->block_creation.account, rai::test_genesis_key.pub.to_account ().c_str ());
	QTest::keyClicks (wallet->block_creation.amount, "100000000000000000000");
	QTest::keyClicks (wallet->block_creation.destination, key.pub.to_account ().c_str ());
	QTest::mouseClick (wallet->block_creation.create, Qt::LeftButton);
	std::string json (wallet->block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json.empty ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (json);
	boost::property_tree::read_json (istream, tree1);
	bool error;
	rai::send_block send (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (send).code);
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->process (send).code);
}

TEST (wallet, create_open_receive)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 100);
	rai::block_hash latest1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 100);
	rai::block_hash latest2 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_NE (latest1, latest2);
	system.wallet (0)->insert_adhoc (key.prv);
	auto account (rai::test_genesis_key.pub);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
	wallet->client_window->show ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet->advanced.create_block, Qt::LeftButton);
	QTest::mouseClick (wallet->block_creation.open, Qt::LeftButton);
	QTest::keyClicks (wallet->block_creation.source, latest1.to_string ().c_str ());
	QTest::keyClicks (wallet->block_creation.representative, rai::test_genesis_key.pub.to_account ().c_str ());
	QTest::mouseClick (wallet->block_creation.create, Qt::LeftButton);
	std::string json1 (wallet->block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json1.empty ());
	boost::property_tree::ptree tree1;
	std::stringstream istream1 (json1);
	boost::property_tree::read_json (istream1, tree1);
	bool error;
	rai::open_block open (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (open).code);
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->process (open).code);
	wallet->block_creation.block->clear ();
	wallet->block_creation.source->clear ();
	QTest::mouseClick (wallet->block_creation.receive, Qt::LeftButton);
	QTest::keyClicks (wallet->block_creation.source, latest2.to_string ().c_str ());
	QTest::mouseClick (wallet->block_creation.create, Qt::LeftButton);
	std::string json2 (wallet->block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json2.empty ());
	boost::property_tree::ptree tree2;
	std::stringstream istream2 (json2);
	boost::property_tree::read_json (istream2, tree2);
	bool error2;
	rai::receive_block receive (error2, tree2);
	ASSERT_FALSE (error2);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (receive).code);
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->process (receive).code);
}

TEST (wallet, create_change)
{
	rai::keypair key;
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto account (rai::test_genesis_key.pub);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
	wallet->client_window->show ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	QTest::mouseClick (wallet->advanced.create_block, Qt::LeftButton);
	QTest::mouseClick (wallet->block_creation.change, Qt::LeftButton);
	QTest::keyClicks (wallet->block_creation.account, rai::test_genesis_key.pub.to_account ().c_str ());
	QTest::keyClicks (wallet->block_creation.representative, key.pub.to_account ().c_str ());
	QTest::mouseClick (wallet->block_creation.create, Qt::LeftButton);
	std::string json (wallet->block_creation.block->toPlainText ().toStdString ());
	ASSERT_FALSE (json.empty ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (json);
	boost::property_tree::read_json (istream, tree1);
	bool error (false);
	rai::change_block change (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (change).code);
	ASSERT_EQ (rai::process_result::old, system.nodes [0]->process (change).code);
}

TEST (history, short_text)
{
	bool init;
	rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
	rai::genesis genesis;
	rai::ledger ledger (store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		genesis.initialize (transaction, store);
		rai::keypair key;
		rai::send_block send (ledger.latest (transaction, rai::test_genesis_key.pub), rai::test_genesis_key.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send).code);
		rai::receive_block receive (send.hash (), send.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, receive).code);
		rai::change_block change (receive.hash (), key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, change).code);
	}
	rai_qt::history history (ledger, rai::test_genesis_key.pub, rai::Gxrb_ratio);
	history.refresh ();
	ASSERT_EQ (4, history.model->rowCount ());
}

TEST (wallet, startup_work)
{
	rai::keypair key;
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::account account;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
    QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	uint64_t work1;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_TRUE (wallet->wallet_m->store.work_get (transaction, rai::test_genesis_key.pub, work1));
	}
	QTest::mouseClick (wallet->accounts_button, Qt::LeftButton);
	QTest::keyClicks (wallet->accounts.account_key_line, "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4");
    QTest::mouseClick (wallet->accounts.account_key_button, Qt::LeftButton);
    auto iterations1 (0);
	auto again (true);
    while (again)
    {
        system.poll ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = wallet->wallet_m->store.work_get (transaction, rai::test_genesis_key.pub, work1);
    }
}

TEST (wallet, block_viewer)
{
	rai::keypair key;
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::account account;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account = system.account (transaction, 0);
	}
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
    QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	ASSERT_NE (-1, wallet->advanced.layout->indexOf (wallet->advanced.block_viewer));
	QTest::mouseClick (wallet->advanced.block_viewer, Qt::LeftButton);
	ASSERT_EQ (wallet->block_viewer.window, wallet->main_stack->currentWidget ());
	rai::block_hash latest (system.nodes [0]->latest (rai::genesis_account));
	QTest::keyClicks (wallet->block_viewer.hash, latest.to_string ().c_str ());
	QTest::mouseClick (wallet->block_viewer.retrieve, Qt::LeftButton);
	ASSERT_FALSE (wallet->block_viewer.block->toPlainText ().toStdString ().empty ());
	QTest::mouseClick (wallet->block_viewer.back, Qt::LeftButton);
	ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
}

TEST (wallet, import)
{
    rai::system system (24000, 2);
	std::string json;
	rai::keypair key1;
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (key1.prv);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		system.wallet (0)->store.serialize_json (transaction, json);
	}
	system.wallet (1)->insert_adhoc (key2.prv);
	auto path (rai::unique_path ());
	{
		std::ofstream stream;
		stream.open (path.string ().c_str ());
		stream << json;
	}
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [1], system.wallet (1), key2.pub));
	wallet->start ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts_button, Qt::LeftButton);
	ASSERT_EQ (wallet->accounts.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts.import_wallet, Qt::LeftButton);
	ASSERT_EQ (wallet->import.window, wallet->main_stack->currentWidget ());
	QTest::keyClicks (wallet->import.filename, path.string ().c_str ());
	QTest::keyClicks (wallet->import.password, "");
	ASSERT_FALSE (system.wallet (1)->exists (key1.pub));
	QTest::mouseClick (wallet->import.perform, Qt::LeftButton);
	ASSERT_TRUE (system.wallet (1)->exists (key1.pub));
}

TEST (wallet, republish)
{
    rai::system system (24000, 2);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	rai::block_hash hash;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		rai::send_block block (system.nodes [0]->ledger.latest (transaction, rai::test_genesis_key.pub), key.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		hash = block.hash ();
		ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (transaction, block).code);
	}
	auto account (rai::test_genesis_key.pub);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), account));
	wallet->start ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->advanced.block_viewer, Qt::LeftButton);
	ASSERT_EQ (wallet->block_viewer.window, wallet->main_stack->currentWidget ());
	QTest::keyClicks (wallet->block_viewer.hash, hash.to_string ().c_str ());
	QTest::mouseClick (wallet->block_viewer.rebroadcast, Qt::LeftButton);
	ASSERT_FALSE (system.nodes [1]->balance (rai::test_genesis_key.pub).is_zero ());
	int iterations (0);
	while (system.nodes [1]->balance (rai::test_genesis_key.pub).is_zero ())
	{
		++iterations;
		ASSERT_LT (iterations, 200);
		system.poll ();
	}
}

TEST (wallet, ignore_empty_adhoc)
{
    rai::system system (24000, 1);
	rai::keypair key1;
	system.wallet (0)->insert_adhoc (key1.prv);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), key1.pub));
	wallet->start ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts_button, Qt::LeftButton);
	ASSERT_EQ (wallet->accounts.window, wallet->main_stack->currentWidget ());
	QTest::keyClicks (wallet->accounts.account_key_line, rai::test_genesis_key.prv.data.to_string ().c_str ());
	QTest::mouseClick (wallet->accounts.account_key_button, Qt::LeftButton);
	ASSERT_EQ (1, wallet->accounts.model->rowCount ());
	ASSERT_EQ (0, wallet->accounts.account_key_line->text ().length ());
	rai::keypair key;
	QTest::keyClicks (wallet->accounts.account_key_line, key.prv.data.to_string ().c_str ());
	QTest::mouseClick (wallet->accounts.account_key_button, Qt::LeftButton);
	ASSERT_EQ (1, wallet->accounts.model->rowCount ());
	ASSERT_EQ (0, wallet->accounts.account_key_line->text ().length ());
	QTest::mouseClick (wallet->accounts.create_account, Qt::LeftButton);
	ASSERT_EQ (2, wallet->accounts.model->rowCount ());
}

TEST (wallet, change_seed)
{
    rai::system system (24000, 1);
	auto key1 (system.wallet (0)->deterministic_insert ());
	auto key3 (system.wallet (0)->deterministic_insert ());
	rai::raw_key seed3;
	system.wallet (0)->store.seed (seed3, rai::transaction (system.wallet (0)->store.environment, nullptr, false));
	auto wallet_key (key1);
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), wallet_key));
	wallet->start ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts_button, Qt::LeftButton);
	ASSERT_EQ (wallet->accounts.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts.import_wallet, Qt::LeftButton);
	ASSERT_EQ (wallet->import.window, wallet->main_stack->currentWidget ());
	rai::raw_key seed;
	seed.data.clear ();
	QTest::keyClicks (wallet->import.seed, seed.data.to_string ().c_str ());
	rai::raw_key seed1;
	system.wallet (0)->store.seed (seed1, rai::transaction (system.wallet (0)->store.environment, nullptr, false));
	ASSERT_NE (seed, seed1);
	ASSERT_TRUE (system.wallet (0)->exists (key1));
	ASSERT_EQ (2, wallet->accounts.model->rowCount ());
	QTest::mouseClick (wallet->import.import_seed, Qt::LeftButton);
	ASSERT_EQ (2, wallet->accounts.model->rowCount ());
	QTest::keyClicks (wallet->import.clear_line, "clear keys");
	QTest::mouseClick (wallet->import.import_seed, Qt::LeftButton);
	ASSERT_EQ (1, wallet->accounts.model->rowCount ());
	ASSERT_TRUE (wallet->import.clear_line->text ().toStdString ().empty ());
	rai::raw_key seed2;
	system.wallet (0)->store.seed (seed2, rai::transaction (system.wallet (0)->store.environment, nullptr, false));
	ASSERT_EQ (seed, seed2);
	ASSERT_FALSE (system.wallet (0)->exists (key1));
	ASSERT_NE (key1, wallet->account);
	auto key2 (wallet->account);
	ASSERT_TRUE (system.wallet (0)->exists (key2));
	QTest::keyClicks (wallet->import.seed, seed3.data.to_string ().c_str ());
	QTest::keyClicks (wallet->import.clear_line, "clear keys");
	QTest::mouseClick (wallet->import.import_seed, Qt::LeftButton);
	ASSERT_EQ (key1, wallet->account);
	ASSERT_FALSE (system.wallet (0)->exists (key2));
	ASSERT_TRUE (system.wallet (0)->exists (key1));
}

TEST (wallet, seed_work_generation)
{
    rai::system system (24000, 1);
	auto key1 (system.wallet (0)->deterministic_insert ());
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), key1));
	wallet->start ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts_button, Qt::LeftButton);
	ASSERT_EQ (wallet->accounts.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts.import_wallet, Qt::LeftButton);
	ASSERT_EQ (wallet->import.window, wallet->main_stack->currentWidget ());
	rai::raw_key seed;
	seed.data.clear ();
	QTest::keyClicks (wallet->import.seed, seed.data.to_string ().c_str ());
	QTest::keyClicks (wallet->import.clear_line, "clear keys");
	QTest::mouseClick (wallet->import.import_seed, Qt::LeftButton);
	auto iterations (0);
	uint64_t work_start;
	system.wallet (0)->store.work_get (rai::transaction (system.wallet (0)->store.environment, nullptr, false), key1, work_start);
	uint64_t work (work_start);
	while (work == work_start)
	{
		system.poll ();
		system.wallet (0)->store.work_get (rai::transaction (system.wallet (0)->store.environment, nullptr, false), key1, work);
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	ASSERT_FALSE (rai::work_validate (system.nodes [0]->ledger.latest_root (rai::transaction (system.wallet (0)->store.environment, nullptr, false), key1), work));
}

TEST (wallet, backup_seed)
{
    rai::system system (24000, 1);
	auto key1 (system.wallet (0)->deterministic_insert ());
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), key1));
	wallet->start ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts_button, Qt::LeftButton);
	ASSERT_EQ (wallet->accounts.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts.backup_seed, Qt::LeftButton);
	rai::raw_key seed;
	system.wallet (0)->store.seed (seed, rai::transaction (system.wallet (0)->store.environment, nullptr, false));
	ASSERT_EQ (seed.data.to_string (), test_application->clipboard ()->text ().toStdString ());
}

TEST (wallet, import_locked)
{
    rai::system system (24000, 1);
	auto key1 (system.wallet (0)->deterministic_insert ());
	system.wallet (0)->store.rekey (rai::transaction (system.wallet (0)->store.environment, nullptr, true), "1");
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system.nodes [0], system.wallet (0), key1));
	wallet->start ();
	QTest::mouseClick (wallet->show_advanced, Qt::LeftButton);
	ASSERT_EQ (wallet->advanced.window, wallet->main_stack->currentWidget ());
	QTest::mouseClick (wallet->accounts_button, Qt::LeftButton);
	ASSERT_EQ (wallet->accounts.window, wallet->main_stack->currentWidget ());
	rai::raw_key seed1;
	seed1.data.clear ();
	QTest::keyClicks (wallet->import.seed, seed1.data.to_string ().c_str ());
	QTest::keyClicks (wallet->import.clear_line, "clear keys");
	system.wallet (0)->enter_password ("");
	QTest::mouseClick (wallet->import.import_seed, Qt::LeftButton);
	rai::raw_key seed2;
	system.wallet (0)->store.seed (seed2, rai::transaction (system.wallet (0)->store.environment, nullptr, false));
	ASSERT_NE (seed1, seed2);
	system.wallet (0)->enter_password ("1");
	QTest::mouseClick (wallet->import.import_seed, Qt::LeftButton);
	rai::raw_key seed3;
	system.wallet (0)->store.seed (seed3, rai::transaction (system.wallet (0)->store.environment, nullptr, false));
	ASSERT_EQ (seed1, seed3);
}

TEST (wallet, synchronizing)
{
    rai::system system0 (24000, 1);
    rai::system system1 (24001, 1);
	auto key1 (system0.wallet (0)->deterministic_insert ());
    auto wallet (std::make_shared <rai_qt::wallet> (*test_application, processor, *system0.nodes [0], system0.wallet (0), key1));
	wallet->start ();
	{
		rai::transaction transaction (system1.nodes [0]->store.environment, nullptr, true);
		auto latest (system1.nodes [0]->ledger.latest (transaction, rai::genesis_account));
		rai::send_block send (latest, key1, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system1.work.generate (latest));
		system1.nodes [0]->ledger.process (transaction, send);
	}
	ASSERT_EQ (0, wallet->active_status.active.count (rai_qt::status_types::synchronizing));
	system0.nodes [0]->bootstrap_initiator.bootstrap (system1.nodes [0]->network.endpoint ());
	auto iterations0 (0);
	while (wallet->active_status.active.count (rai_qt::status_types::synchronizing) == 0)
	{
		system0.poll ();
		system1.poll ();
		test_application->processEvents();
		++iterations0;
		ASSERT_GT (200, iterations0);
	}
	auto iterations1 (0);
	while (wallet->active_status.active.count (rai_qt::status_types::synchronizing) == 1)
	{
		system0.poll ();
		system1.poll ();
		test_application->processEvents();
		++iterations1;
		ASSERT_GT (200, iterations1);
	}
}
