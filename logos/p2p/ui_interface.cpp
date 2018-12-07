// Copyright (c) 2010-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ui_interface.h"
#include <util.h>
#include <logging.h>
#include "p2p.h"

CClientUIInterface uiInterface;

/** Progress message during initialization. */
void CClientUIInterface::InitMessage(const std::string& message)
{
    config->init_print((std::string("Info: ") + message).c_str());
}

/** Number of network connections changed. */
void CClientUIInterface::NotifyNumConnectionsChanged(int newNumConnections)
{
    config->init_print((std::string("Message: number of connections changed to ") + std::to_string(newNumConnections) + ".").c_str());
}

/** Network activity state changed. */
void CClientUIInterface::NotifyNetworkActiveChanged(bool networkActive)
{
    config->init_print((std::string("Message: network is now ") + (networkActive ? "active" : "inactive") + ".").c_str());
}

/** Banlist did change. */
void CClientUIInterface::BannedListChanged()
{
    config->init_print((std::string("Message: banned list changed.")).c_str());
}

bool InitError(const std::string& str)
{
    uiInterface.config->init_print((std::string("Error: ") + str).c_str());
    return false;
}

void InitWarning(const std::string& str)
{
    uiInterface.config->init_print((std::string("Warning: ") + str).c_str());
}

std::string AmountHighWarn(const std::string& optname)
{
    return strprintf(_("%s is set very high!"), optname);
}

std::string AmountErrMsg(const char* const optname, const std::string& strValue)
{
    return strprintf(_("Invalid amount for -%s=<amount>: '%s'"), optname, strValue);
}
