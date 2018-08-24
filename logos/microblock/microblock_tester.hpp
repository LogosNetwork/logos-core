#pragma once
#include <logos/node/node.hpp>

#include <functional>
#include <boost/property_tree/ptree.hpp>

class MicroBlockTester 
{
public:
    static bool microblock_tester(std::string &action, std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void block_create_test(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void precreate_account(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void read_accounts(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void generate_microblock(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
};