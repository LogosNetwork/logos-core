#pragma once

#include <boost/property_tree/ptree.hpp>

struct ConsensusManagerConfig
{

	bool DeserializeJson(boost::property_tree::ptree & tree)
	{
		auto result(false);

		auto stream_peers_tree(tree.get_child("stream_peers"));
		for(auto & entry : stream_peers_tree)
		{
			stream_peers.push_back(entry.second.get<std::string> (""));
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
		boost::property_tree::ptree stream_peers_tree;

		for (auto & peer : stream_peers)
		{
			boost::property_tree::ptree entry;
			entry.put ("", peer);
			stream_peers_tree.push_back (std::make_pair ("", entry));
		}

		tree.add_child("stream_peers", stream_peers_tree);

        tree.put("peer_port", std::to_string(peer_port));
        tree.put("local_address", local_address);
	}

	std::vector<std::string> stream_peers;
    uint16_t                 peer_port;
    std::string              local_address;
};


