#include <rai/qt/qt.hpp>

#include <rai/node/working.hpp>
#include <rai/icon.hpp>
#include <rai/node/rpc.hpp>

#include <boost/make_shared.hpp>

#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

class qt_wallet_config
{
public:
	qt_wallet_config (boost::filesystem::path const & application_path_a) :
	account (0),
	rpc_enable (false),
	opencl_enable (false)
	{
		rai::random_pool.GenerateBlock (wallet.bytes.data (), wallet.bytes.size ());
		assert (!wallet.is_zero ());
	}
	bool upgrade_json (unsigned version_a, boost::property_tree::ptree & tree_a)
	{
		auto result (false);
		switch (version_a)
		{
		case 1:
		{
			rai::account account;
			account.decode_account (tree_a.get <std::string> ("account"));
			tree_a.erase ("account");
			tree_a.put ("account", account.to_account ());
			tree_a.erase ("version");
			tree_a.put ("version", "2");
			result = true;
		}
		case 2:
		{
			boost::property_tree::ptree rpc_l;
			rpc.serialize_json (rpc_l);
			tree_a.put ("rpc_enable", "false");
			tree_a.put_child ("rpc", rpc_l);
			tree_a.erase ("version");
			tree_a.put ("version", "3");
			result = true;
		}
		case 3:
		{
			auto opencl_enable_l (tree_a.get_optional <bool> ("opencl_enable"));
			if (!opencl_enable_l)
			{
				tree_a.put ("opencl_enable", "false");
			}
			auto opencl_l (tree_a.get_child_optional ("opencl"));
			if (!opencl_l)
			{
				boost::property_tree::ptree opencl_l;
				opencl.serialize_json (opencl_l);
				tree_a.put_child ("opencl", opencl_l);
			}
			tree_a.put ("version", "4");
			result = true;
		}
		case 4:
			break;
		default:
			throw std::runtime_error ("Unknown qt_wallet_config version");
		}
		return result;
	}
	bool deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
	{
		auto error (false);
		if (!tree_a.empty ())
		{
			auto version_l (tree_a.get_optional <std::string> ("version"));
			if (!version_l)
			{
				tree_a.put ("version", "1");
				version_l = "1";
				upgraded_a = true;
			}
			upgraded_a |= upgrade_json (std::stoull (version_l.get ()), tree_a);
			auto wallet_l (tree_a.get <std::string> ("wallet"));
			auto account_l (tree_a.get <std::string> ("account"));
			auto & node_l (tree_a.get_child ("node"));
			rpc_enable = tree_a.get <bool> ("rpc_enable");
			auto & rpc_l (tree_a.get_child ("rpc"));
			opencl_enable = tree_a.get <bool> ("opencl_enable");
			auto & opencl_l (tree_a.get_child ("opencl"));
			try
			{
				error |= wallet.decode_hex (wallet_l);
				error |= account.decode_account (account_l);
				error |= node.deserialize_json (upgraded_a, node_l);
				error |= rpc.deserialize_json (rpc_l);
				error |= opencl.deserialize_json (opencl_l);
				if (wallet.is_zero ())
				{
					rai::random_pool.GenerateBlock (wallet.bytes.data (), wallet.bytes.size ());
					upgraded_a = true;
				}
			}
			catch (std::logic_error const &)
			{
				error = true;
			}
		}
		else
		{
			serialize_json (tree_a);
			upgraded_a = true;
		}
		return error;
	}
	void serialize_json (boost::property_tree::ptree & tree_a)
	{
		std::string wallet_string;
		wallet.encode_hex (wallet_string);
		tree_a.put ("version", "4");
		tree_a.put ("wallet", wallet_string);
		tree_a.put ("account", account.to_account ());
		boost::property_tree::ptree node_l;
		node.serialize_json (node_l);
		tree_a.add_child ("node", node_l);
		boost::property_tree::ptree rpc_l;
		rpc.serialize_json (rpc_l);
		tree_a.add_child ("rpc", rpc_l);
		tree_a.put ("rpc_enable", rpc_enable);
		tree_a.put ("opencl_enable", opencl_enable);
		boost::property_tree::ptree opencl_l;
		opencl.serialize_json (opencl_l);
		tree_a.add_child ("opencl", opencl_l);
	}
	bool serialize_json_stream (std::ostream & stream_a)
	{
		auto result (false);
		stream_a.seekp (0);
		try
		{
			boost::property_tree::ptree tree;
			serialize_json (tree);
			boost::property_tree::write_json (stream_a, tree);
		}
		catch (std::runtime_error const &)
		{
			result = true;
		}
		return result;
	}
	rai::uint256_union wallet;
	rai::account account;
	rai::node_config node;
	bool rpc_enable;
	rai::rpc_config rpc;
	bool opencl_enable;
	rai::opencl_config opencl;
};

namespace
{
void show_error (std::string const & message_a)
{
	QMessageBox message (QMessageBox::Critical, "Error starting RaiBlocks", message_a.c_str ());
	message.setModal (true);
	message.show ();
	message.exec ();
}
bool update_config (qt_wallet_config & config_a, boost::filesystem::path const & config_path_a, std::fstream & config_file_a)
{
	auto account (config_a.account);
	auto wallet (config_a.wallet);
	auto error (false);
	if (!rai::fetch_object (config_a, config_path_a, config_file_a))
	{
		if (account != config_a.account || wallet != config_a.wallet)
		{
			config_a.account = account;
			config_a.wallet = wallet;
			config_file_a.close ();
			config_file_a.open (config_path_a.string (), std::ios_base::out | std::ios_base::trunc);
			error = config_a.serialize_json_stream (config_file_a);
		}
	}
	return error;
}
}

int run_wallet (QApplication & application, int argc, char * const * argv, boost::filesystem::path const & data_path)
{
	rai_qt::eventloop_processor processor;
	boost::filesystem::create_directories (data_path);
	QPixmap pixmap(":/logo.png");
	QSplashScreen *splash = new QSplashScreen(pixmap);
	splash->show();
	application.processEvents();
	splash->showMessage(QSplashScreen::tr("Remember - Backup Your Wallet Seed"), Qt::AlignBottom | Qt::AlignHCenter, Qt::black);
	application.processEvents();
	qt_wallet_config config (data_path);
	auto config_path ((data_path / "config.json"));
    int result (0);
	std::fstream config_file;
	auto error (rai::fetch_object (config, config_path, config_file));
	config_file.close ();
	if (!error)
    {
        boost::asio::io_service service;
		config.node.logging.init (data_path);
		std::shared_ptr <rai::node> node;
		std::shared_ptr <rai_qt::wallet> gui;
		rai::set_application_icon (application);
		rai::work_pool work (config.node.work_threads, rai::opencl_work::create (config.opencl_enable, config.opencl, config.node.logging));
		rai::alarm alarm (service);
		rai::node_init init;
		node = std::make_shared <rai::node> (init, service, data_path, alarm, config.node, work);
		if (!init.error ())
		{
			auto wallet (node->wallets.open (config.wallet));
			if (wallet == nullptr)
			{
				auto existing (node->wallets.items.begin ());
				if (existing != node->wallets.items.end ())
				{
					wallet = existing->second;
					config.wallet = existing->first;
				}
				else
				{
					wallet = node->wallets.create (config.wallet);
				}
			}
			if (config.account.is_zero () || !wallet->exists (config.account))
			{
				rai::transaction transaction (wallet->store.environment, nullptr, true);
				auto existing (wallet->store.begin (transaction));
				if (existing != wallet->store.end ())
				{
					rai::uint256_union account (existing->first.uint256 ());
					config.account = account;
				}
				else
				{
					config.account = wallet->deterministic_insert (transaction);
				}
			}
			assert (wallet->exists (config.account));
			update_config (config, config_path, config_file);
			node->start ();
			rai::rpc rpc (service, *node, config.rpc);
			if (config.rpc_enable)
			{
				rpc.start ();
			}
			rai::thread_runner runner (service, node->config.io_threads);
			QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
			{
				rpc.stop ();
				node->stop ();
			});
			application.postEvent (&processor, new rai_qt::eventloop_event ([&] ()
			{
				gui = std::make_shared <rai_qt::wallet> (application, processor, *node, wallet, config.account);
				splash->close();
				gui->start ();
				gui->client_window->show ();
			}));
			result = application.exec ();
			runner.join ();
		}
		else
		{
			show_error ("Error initializing node");
		}
		update_config (config, config_path, config_file);
	}
	else
	{
		show_error ("Error deserializing config");
	}
	return result;
}

int main (int argc, char * const * argv)
{
	try
	{
		QApplication application (argc, const_cast <char **> (argv));
		boost::program_options::options_description description ("Command line options");
		description.add_options () ("help", "Print out options");
		rai::add_node_options (description);
		boost::program_options::variables_map vm;
		boost::program_options::store (boost::program_options::command_line_parser (argc, argv).options (description).allow_unregistered ().run (), vm);
		boost::program_options::notify (vm);
		int result (0);
		if (!rai::handle_node_options (vm))
		{
		}
		else if (vm.count ("help") != 0)
		{
			std::cout << description << std::endl;
		}
		else
		{
			try
			{
				boost::filesystem::path data_path;
				if (vm.count ("data_path"))
				{
					auto name (vm ["data_path"].as <std::string> ());
					data_path = boost::filesystem::path (name);
				}
				else
				{
					data_path = rai::working_path ();
				}
				result = run_wallet (application, argc, argv, data_path);
			}
			catch (std::exception const & e)
			{
				show_error (boost::str (boost::format ("Exception while running wallet: %1%") % e.what ()));
			}
			catch (...)
			{
				show_error ("Unknown exception while running wallet");
			}
		}
		return result;
	}
	catch (std::exception const & e)
	{
		std::cerr << boost::str (boost::format ("Exception while initializing %1%") % e.what ());
	}
	catch (...)
	{
		std::cerr << boost::str (boost::format ("Unknown exception while initializing"));
	}
	return 1;
}
