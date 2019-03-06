/*
#pragma once
#include <vector>
#include <logos/consensus/consensus_p2p.hpp>

namespace Bootstrap
{
    namespace p2p
    {
        int constexpr INVALID_SESSION = -1;
        int constexpr UNKNOWN_PEER = -1;
        int constexpr MAX_PEER_REQUEST = 128;
        int constexpr MAX_BLACKLIST_RETRY = MAX_PEER_REQUEST;

        // get_peers
        // to be called on init call of bootstrap_peer() and to which we will subsequently
        // get peers at random from the vector
        // @param session_id is initialized with an invalid value (-1)
        // @param nodes vector of discovered nodes up to count in length
        // @param count how many peers we are asking for
        // @returns a new session_id, along with a list of peers
        int get_peers(int session_id, std::vector<logos::endpoint> & nodes, uint8_t count);

        // close_session
        // Close session (to be managed in bootstrap_attempt)
        // @param session_id integer id
        void close_session(PeerInfoProvider &peer_provider, int session_id);

        // add_to_blacklist
        // Add a peer to a blacklist
        // to be called when validation fails
        // @param endpoint peer to add
        void add_to_blacklist(PeerInfoProvider &peer_provider, logos::endpoint &e);

        // add_to_blacklist
        // Add a peer to a blacklist
        // to be called when validation fails
        // @param endpoint peer to add
        void add_to_blacklist(PeerInfoProvider &peer_provider, logos::endpoint &e);

        // is_blacklisted
        // @param endpoint of peer to check
        // @returns true if peer is in the blacklist
        // to be checked when we select a new peer to bootstrap from
        bool is_blacklisted(PeerInfoProvider &peer_provider, logos::endpoint &e);

        // get_peers
        // wrapper for bootstrap to get peers, vector stored internally.
        // @returns session_id to be closed in bootstrap_attempt
        int get_peers(PeerInfoProvider &peer_provider);

        // add_peer
        // add a selected peer uniquely to our cache
        // @param endpoint
        void add_peer(logos::endpoint &e);

        // get_peer_id
        // @param endpoint
        // @returns unique integer representing peer based on internal map.
        int get_peer_id(logos::endpoint &e);

        // get_peer_id
        // @param v6 string representing endpoint
        // @returns unique integer representing peer based on internal map.
        int get_peer_id(std::string address_str);

        // get_random_peer
        // @returns a peer selected at random from the vector
        logos::endpoint get_random_peer();
    }
}

*/