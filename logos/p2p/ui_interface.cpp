// Copyright (c) 2010-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ui_interface.h"
#include <util.h>
#include <logging.h>
#include "p2p.h"

/** Progress message during initialization. */
void CClientUIInterface::InitMessage(const std::string& message)
{
    config.userInterfaceMessage(P2P_UI_INFO|P2P_UI_INIT, message.c_str());
}

/** Number of network connections changed. */
void CClientUIInterface::NotifyNumConnectionsChanged(int newNumConnections)
{
    std::string message = "number of connections changed to ";
    message += std::to_string(newNumConnections);
    config.userInterfaceMessage(P2P_UI_INFO, message.c_str());
}

/** Network activity state changed. */
void CClientUIInterface::NotifyNetworkActiveChanged(bool networkActive)
{
    std::string message = "network is now ";
    message += (networkActive ? "active" : "inactive");
    config.userInterfaceMessage(P2P_UI_INFO, message.c_str());
}

/** Banlist did change. */
void CClientUIInterface::BannedListChanged()
{
    std::string message = "banned list changed ";
    config.userInterfaceMessage(P2P_UI_INFO, message.c_str());
}

bool CClientUIInterface::InitError(const std::string& str)
{
    config.userInterfaceMessage(P2P_UI_ERROR|P2P_UI_INIT, str.c_str());
    return false;
}

void CClientUIInterface::InitWarning(const std::string& str)
{
    config.userInterfaceMessage(P2P_UI_WARNING|P2P_UI_INIT, str.c_str());
}

