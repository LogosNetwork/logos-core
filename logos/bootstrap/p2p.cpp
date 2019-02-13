#include <logos/node/common.hpp>
#include <logos/bootstrap/p2p.hpp>
#include <mutex>
#include <vector>
#include <string>
#include <map>

static std::mutex smutex;
static std::vector<logos::endpoint>     nodes_vector;
static std::map<std::string, int>       id_map;
static std::map<int,logos::endpoint>    endpoint_map;
static int id           = 0;
static int next_peer    = 0;

int p2p::get_peers(int session_id, std::vector<logos::endpoint> & nodes, uint8_t count)
{
    // TODO Implement by calling real p2p...
    return 0;
}

void p2p::close_session(int session_id)
{
    std::lock_guard<std::mutex> lock(smutex);
    // TODO Implement by calling real p2p...
    nodes_vector.clear();
    next_peer = 0;
}

void p2p::add_to_blacklist(logos::endpoint &e)
{
    // TODO Implement by calling real p2p...
}

void p2p::add_to_blacklist(int peer)
{
    std::lock_guard<std::mutex> lock(smutex);
    if(endpoint_map.find(peer) != endpoint_map.end()) {
        p2p::add_to_blacklist(endpoint_map[peer]); 
    }
}

bool p2p::is_blacklisted(logos::endpoint & e)
{
    // TODO Implement by calling real p2p...
    return false;
}

void p2p::add_peer(logos::endpoint &e)
{
    std::lock_guard<std::mutex> lock(smutex);
    auto address = e.address();
    auto address_str = address.to_v6().to_string();
    if(id_map.find(address_str) == id_map.end()) {
        auto next = ++id;
        id_map[address_str] = next;
        endpoint_map[next]  = e;
    }
}

int p2p::get_peer_id(logos::endpoint &e)
{
    std::lock_guard<std::mutex> lock(smutex);
    auto address = e.address();
    auto address_str = address.to_v6().to_string();
    if(id_map.find(address_str) == id_map.end()) {
        return UNKNOWN_PEER;
    }
    return id_map[address_str];
}

int p2p::get_peer_id(std::string address_str)
{
    std::lock_guard<std::mutex> lock(smutex);
    if(id_map.find(address_str) == id_map.end()) {
        return UNKNOWN_PEER;
    }
    return id_map[address_str];
}

int p2p::get_peers()
{
    std::lock_guard<std::mutex> lock(smutex);
    int session = p2p::get_peers(p2p::INVALID_SESSION, nodes_vector, p2p::MAX_PEER_REQUEST);
    return session;
}

logos::endpoint p2p::get_random_peer()
{
    std::lock_guard<std::mutex> lock(smutex);
    if(nodes_vector.size() <= 0) {
        // Nothing here...
        return logos::endpoint(boost::asio::ip::address_v6::any(),0);
    }
    // Pick a peer to return.
    return nodes_vector[next_peer++ % nodes_vector.size()];
}
