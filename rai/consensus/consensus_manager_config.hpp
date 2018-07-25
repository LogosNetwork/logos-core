#pragma once

#include <boost/property_tree/ptree.hpp>

struct ConsensusManagerConfig
{

	bool DeserializeJson(boost::property_tree::ptree & tree)
	{
		auto result(false);

		auto delegates_tree(tree.get_child("delegates"));
		for(auto & entry : delegates_tree)
		{
			delegates.push_back(entry.second.get<std::string> (""));
		}

		local_address = tree.get<std::string>("local_address");

        try
        {
            peer_port = std::stoul(tree.get<std::string>("peer_port"));
        }
        catch (std::logic_error const &)
        {
            result = true;
        }

		return result;
	}

	void SerializeJson(boost::property_tree::ptree & tree) const
	{
		boost::property_tree::ptree delegates_tree;

		for (auto & peer : delegates)
		{
			boost::property_tree::ptree entry;
			entry.put ("", peer);
			delegates_tree.push_back (std::make_pair ("", entry));
		}

		tree.add_child("delegates", delegates_tree);

        tree.put("peer_port", std::to_string(peer_port));
        tree.put("local_address", local_address);
	}

	std::vector<std::string> delegates;
    uint16_t                 peer_port;
    std::string              local_address;
};


