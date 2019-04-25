// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparamsbase.h>

#include <tinyformat.h>
#include <util.h>
#include <utilmemory.h>

#include <assert.h>

const std::string CBaseChainParams::MAIN = "main";
const std::string CBaseChainParams::TESTNET = "test";
const std::string CBaseChainParams::REGTEST = "regtest";

std::shared_ptr<CBaseChainParams> CreateBaseChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::make_shared<CBaseChainParams>("", 8332);
    else if (chain == CBaseChainParams::TESTNET)
        return std::make_shared<CBaseChainParams>("testnet3", 18332);
    else if (chain == CBaseChainParams::REGTEST)
        return std::make_shared<CBaseChainParams>("regtest", 18443);
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

std::shared_ptr<CBaseChainParams> SelectBaseParams(ArgsManager &Args, const std::string& chain)
{
    auto globalChainBaseParams = CreateBaseChainParams(chain);
    Args.SelectConfigNetwork(chain);
    return globalChainBaseParams;
}
