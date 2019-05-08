// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CLIENTVERSION_H
#define BITCOIN_CLIENTVERSION_H

#include <config/bitcoin-config.h>
#include <string>

constexpr int CLIENT_VERSION =
        1000000 * CLIENT_VERSION_MAJOR
        +   10000 * CLIENT_VERSION_MINOR
        +     100 * CLIENT_VERSION_REVISION
        +       1 * CLIENT_VERSION_BUILD;

std::string FormatFullVersion();

#endif // BITCOIN_CLIENTVERSION_H
