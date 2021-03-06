#include <logos/node/node.hpp>
#include <logos/node/rpc.hpp>
#include <logos/p2p/p2p.h>

namespace logos_daemon
{
class daemon
{
public:
    void run (boost::filesystem::path const &, const p2p_config &);
    void run_tx_acceptor(boost::filesystem::path const &);
};
class daemon_config
{
public:
    daemon_config (boost::filesystem::path const &);
    bool deserialize_json (bool &, boost::property_tree::ptree &);
    void serialize_json (boost::property_tree::ptree &);
    bool upgrade_json (unsigned, boost::property_tree::ptree &);
    bool rpc_enable;
    logos::rpc_config rpc;
    logos::node_config node;
    p2p_config p2p_conf;
};
}
