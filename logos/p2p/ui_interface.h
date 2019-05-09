// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2012-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UI_INTERFACE_H
#define BITCOIN_UI_INTERFACE_H

#include <stdint.h>
#include <functional>
#include <memory>
#include <string>

/** Signals for UI communication. */
class CClientUIInterface
{
private:
    struct p2p_config & config;

public:
    CClientUIInterface(struct p2p_config &configIn)
        : config(configIn)
    {
    }

    /** Progress message during initialization. */
    void InitMessage(const std::string& message);

    /** Show warning message **/
    void InitWarning(const std::string& str);

    /** Show error message **/
    bool InitError(const std::string& str);

    /** Number of network connections changed. */
    void NotifyNumConnectionsChanged(int newNumConnections);

    /** Network activity state changed. */
    void NotifyNetworkActiveChanged(bool networkActive);

    /** Banlist did change. */
    void BannedListChanged();
};

#endif // BITCOIN_UI_INTERFACE_H
