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

        } catch (...)
        {}

        std::string ip = "";
        try { // temp for backward compatability, parses ConsensusManager
            ip = tree.get<std::string>("local_address");
        } catch (...) {}

        json_port = tree.get<uint16_t>("json_port", 56001);
        bin_port = tree.get<uint16_t>("bin_port", 56002);
        delegate_ip = tree.get<std::string>("delegate_ip", ip);
        acceptor_ip = tree.get<std::string>("acceptor_ip", ip);
        port = tree.get<uint16_t>("port", 56000);
        validate_sig = tree.get<bool>("validate_sig", false);
        max_connections = tree.get<uint32_t>("max_connections", UINT32_MAX);
        bls_pub = tree.get<std::string>("bls_pub", "");

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
        tree.put("validate_sig", validate_sig);
        tree.put("max_connections", max_connections);
        tree.put("bls_pub", bls_pub);

        return false;
    }

    std::vector<Acceptor> tx_acceptors;                 /// list of delegate's tx-acceptors
    std::string           delegate_ip;                  /// delegate's ip for verification
    std::string           acceptor_ip;                  /// tx-acceptor ip to accept delegate's connection
    uint16_t              port=56000;                   /// tx-acceptor port to accept delegate's connection
    uint16_t              json_port=56001;              /// port to receive json formatted transactions
    uint16_t              bin_port=56002;               /// port to receive binary formatted transactions
    bool                  validate_sig=false;           /// if true then delegate validates transaction's signature
    uint32_t              max_connections = UINT32_MAX; /// max allowed client connections
    std::string           bls_pub;                      /// bls public key
};
