#pragma once

#include <vector>
#include <logos/node/common.hpp>

class PeerInfoProvider
{
public:
    static constexpr int GET_PEER_NEW_SESSION = -1;

    /* Where session_id is initialized with an invalid value (-1) and a new session_id
     * is returned by the function, along with a list of peers. count indicates how
     * many peers we are asking for.
     * to be called on init call of bootstrap_peer() and to which we will subsequently
     * get peers at random from the vector.
     * The reason of adding the session when get_peers() is so that the caller doesn't
     * get repeated endpoints. To create a new session, the function is called with
     * an invalid session_id (e.g., #define GET_PEER_NEW_SESSION -1), and the function
     * should create a new session and return a valid session_id.
     */
    virtual int get_peers(int session_id, vector<logos::endpoint> & nodes, uint8_t count) = 0;

    /* Close session (to be managed in bootstrap_attempt) */
    virtual void close_session(int session_id) = 0;

    /* Add a peer to a blacklist
     * to be called when validation fails
     */
    virtual void add_to_blacklist(const logos::endpoint & e) = 0;

    /* true if peer is in the blacklist
     * to be checked when we select a new peer to bootstrap from
     */
    virtual bool is_blacklisted(const logos::endpoint & e) = 0;
};

