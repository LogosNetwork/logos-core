#pragma once

#include <boost/property_tree/ptree.hpp>
#include <logos/consensus/messages/messages.hpp>

struct ConsensusManagerConfig
{

    struct Delegate
    {
        std::string ip;
        uint8_t     id;
    };

    bool DeserializeJson(boost::property_tree::ptree & tree)
    {
        auto rpc_tree(tree.get_child(""));

        auto delegates_tree(tree.get_child("delegate_peers"));

        uint8_t num_consensus_delegates = tree.get<uint8_t>("num_consensus_delegates", NUM_DELEGATES);

        for(auto & delegate : delegates_tree)
        {
            try
            {
                auto ip = delegate.second.get<std::string>("ip_address");
                auto id = std::stoul(delegate.second.get<std::string>("delegate_id"));

                if (delegates.size() < num_consensus_delegates)
                {
                    delegates.push_back(Delegate{ip, uint8_t(id)});
                }
                all_delegates.push_back(Delegate{ip, uint8_t(id)});
            }
            catch(std::logic_error const &)
            {
                return true;
            }
        }

        local_address = tree.get<std::string>("local_address");
        callback_address = tree.get<std::string>("callback_address");

        try
        {
            peer_port = std::stoul(tree.get<std::string>("peer_port"));
          
            callback_port = std::stoul(tree.get<std::string>("callback_port"));

            delegate_id = std::stoul(tree.get<std::string>("delegate_id"));
        }
        catch(std::logic_error const &)
        {
            return true;
        }

        return false;
    }

    // TODO: Test serialization
    //
    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        boost::property_tree::ptree delegates_tree;

        for (auto & delegate : delegates)
        {
            boost::property_tree::ptree entry;

            entry.put("ip_address", delegate.ip);
            entry.put("delegate_id", delegate.id);

            delegates_tree.push_back(std::make_pair("", entry));
        }

        tree.add_child("delegate_peers", delegates_tree);

        tree.put("local_address", local_address);
        tree.put("callback_address", callback_address);
        tree.put("callback_port", std::to_string(callback_port));
        tree.put("peer_port", std::to_string(peer_port));
        tree.put("delegate_id", std::to_string(delegate_id));
    }

    std::vector<Delegate> delegates;
    std::vector<Delegate> all_delegates; /// ip's/id's of all delegates
    std::string           local_address;
    std::string           callback_address;
    uint16_t              callback_port;
    uint16_t              peer_port;
    uint8_t               delegate_id;
    bool                  run_local;    /// run nodes locally with multiple ip (for testing)
};
