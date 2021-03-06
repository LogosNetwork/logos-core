#pragma once
#include <logos/node/node.hpp>

#include <functional>
#include <boost/property_tree/ptree.hpp>

class MicroBlockTester 
{
public:
    static bool microblock_tester(std::string &action, boost::property_tree::ptree request,
            std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void block_create_test(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void precreate_account(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void read_accounts(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void generate_microblock(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void generate_epoch(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void start_epoch_transition(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void informational(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void epoch_delegates(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static void advertise(std::function<void(boost::property_tree::ptree const &)> response, logos::node &node);
    static boost::property_tree::ptree _request;
};