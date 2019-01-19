// @file
// Defines TxAcceptor configuration
//

#pragma once

#include <boost/property_tree/ptree.hpp>
#include <vector>

struct TxAcceptorConfig
{
    struct Acceptor
    {
        std::string ip;
        uint16_t    port;
    };

    bool DeserializeJson(boost::property_tree::ptree & tree)
    {
        try
        {
            auto tx_acceptors_tree(tree.get_child("tx_acceptors"));

            for (auto &tx_acceptor : tx_acceptors_tree) {
                tx_acceptors.push_back(Acceptor{tx_acceptor.second.get<std::string>("ip"),
                                               tx_acceptor.second.get<uint16_t>("port")
                });
            }

        } catch (std::logic_error const &)
        {}

        json_port = tree.get<uint16_t>("json_port");
        bin_port = tree.get<uint16_t>("bin_port");
        delegate_ip = tree.get<std::string>("delegate_ip");
        acceptor_ip = tree.get<std::string>("acceptor_ip");
        port = tree.get<uint16_t>("port");

        return false;
    }

    bool SerializeJson(boost::property_tree::ptree & tree) const
    {
        boost::property_tree::ptree tx_acceptors_tree;

        for (auto & tx_acceptor : tx_acceptors)
        {
            boost::property_tree::ptree entry;

            entry.put("ip", tx_acceptor.ip);
            entry.put("port", tx_acceptor.port);

            tx_acceptors_tree.push_back(std::make_pair("", entry));
        }

        tree.add_child("tx_acceptors", tx_acceptors_tree);
        tree.put("json_port", std::to_string(json_port));
        tree.put("bin_port", std::to_string(bin_port));
        tree.put("delegate_ip", delegate_ip);
        tree.put("acceptor_ip", acceptor_ip);
        tree.put("port", port);

        return false;
    }

    std::vector<Acceptor> tx_acceptors;
    std::string           delegate_ip;
    std::string           acceptor_ip;
    uint16_t              port;
    uint16_t              json_port;
    uint16_t              bin_port;
};
