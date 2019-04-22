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

        heartbeat = tree.get<bool>("heartbeat", true);

        enable_elections = tree.get<bool>("enable_elections", false);

        enable_epoch_transition = tree.get<bool>("enable_epoch_transition", true);

        return false;
    }

    // TODO: Test serialization
    //
    void SerializeJson(boost::property_tree::ptree & tree) const
    {
        boost::property_tree::ptree delegates_tree;

        tree.put("local_address", local_address);
        tree.put("callback_address", callback_address);
        tree.put("callback_port", std::to_string(callback_port));
        tree.put("peer_port", std::to_string(peer_port));
        tree.put("delegate_id", std::to_string(delegate_id));
        tree.put("heartbeat", std::to_string(heartbeat));
        tree.put("enable_elections", std::to_string(enable_elections));
        tree.put("enable_epoch_transition", std::to_string(enable_epoch_transition));
    }

    std::vector<Delegate> delegates;
    std::vector<Delegate> all_delegates; /// ip's/id's of all delegates
    std::string           local_address;
    std::string           callback_address;
    uint16_t              callback_port;
    uint16_t              peer_port;
    uint8_t               delegate_id;
    bool                  heartbeat;
    bool                  enable_elections;
    bool                  enable_epoch_transition;
};
