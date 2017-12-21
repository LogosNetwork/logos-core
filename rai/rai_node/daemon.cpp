#include <rai/rai_node/daemon.hpp>

#include <rai/node/working.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <fstream>

rai_daemon::daemon_config::daemon_config (boost::filesystem::path const & application_path_a) :
rpc_enable (false),
opencl_enable (false)
{
}

void rai_daemon::daemon_config::serialize_json (boost::property_tree::ptree & tree_a)
{
	tree_a.put ("version", "2");
	tree_a.put ("rpc_enable", rpc_enable);
	boost::property_tree::ptree rpc_l;
	rpc.serialize_json (rpc_l);
	tree_a.add_child ("rpc", rpc_l);
	boost::property_tree::ptree node_l;
	node.serialize_json (node_l);
	tree_a.add_child ("node", node_l);
	tree_a.put ("opencl_enable", opencl_enable);
	boost::property_tree::ptree opencl_l;
	opencl.serialize_json (opencl_l);
	tree_a.add_child ("opencl", opencl_l);
}

bool rai_daemon::daemon_config::deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
{
    auto error (false);
	try
	{
		if (!tree_a.empty ())
		{
			auto version_l (tree_a.get_optional <std::string> ("version"));
			if (!version_l)
			{
				tree_a.put ("version", "1");
				version_l = "1";
			}
			upgraded_a |= upgrade_json (std::stoull (version_l.get ()), tree_a);
			rpc_enable = tree_a.get <bool> ("rpc_enable");
			auto rpc_l (tree_a.get_child ("rpc"));
			error |= rpc.deserialize_json (rpc_l);
			auto & node_l (tree_a.get_child ("node"));
			error |= node.deserialize_json (upgraded_a, node_l);
			opencl_enable = tree_a.get <bool> ("opencl_enable");
			auto & opencl_l (tree_a.get_child ("opencl"));
			error |= opencl.deserialize_json (opencl_l);
		}
		else
		{
			upgraded_a = true;
			serialize_json (tree_a);
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}


bool rai_daemon::daemon_config::upgrade_json (unsigned version_a, boost::property_tree::ptree & tree_a)
{
	auto result (false);
	switch (version_a)
	{
	case 1:
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
		tree_a.put ("version", "2");
		result = true;
	}
	case 2:
		break;
	default:
		throw std::runtime_error ("Unknown daemon_config version");
	}
	return result;
}

void rai_daemon::daemon::run (boost::filesystem::path const & data_path)
{
	boost::filesystem::create_directories (data_path);
    rai_daemon::daemon_config config (data_path);
    auto config_path ((data_path / "config.json"));
    std::fstream config_file;
    std::unique_ptr <rai::thread_runner> runner;
	auto error (rai::fetch_object (config, config_path, config_file));
	if (!error)
	{
		config.node.logging.init (data_path);
		config_file.close ();
		boost::asio::io_service service;
		auto opencl (rai::opencl_work::create (config.opencl_enable, config.opencl, config.node.logging));
		rai::work_pool opencl_work (config.node.work_threads, opencl ? [&opencl] (rai::uint256_union const & root_a)
		{
			return opencl->generate_work (root_a);
		} : std::function <boost::optional <uint64_t> (rai::uint256_union const &)> (nullptr));
		rai::alarm alarm (service);
		rai::node_init init;
		auto node (std::make_shared <rai::node> (init, service, data_path, alarm, config.node, opencl_work));
		if (!init.error ())
		{
			node->start ();
			rai::rpc rpc (service, *node, config.rpc);
			if (config.rpc_enable)
			{
				rpc.start ();
			}
			runner.reset (new rai::thread_runner (service, node->config.io_threads));
			runner->join ();
		}
		else
		{
			std::cerr << "Error initializing node\n";
		}
	}
	else
	{
		std::cerr << "Error deserializing config\n";
	}
}
