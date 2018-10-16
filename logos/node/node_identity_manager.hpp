/// @file
/// This file contains the declaration of the NodeIdentityManager class, which encapsulates
/// node identity management logic. Currently it holds all delegates ip, accounts, and delegate's index (formerly id)
/// into epoch's voted delegates. It also creates genesis microblocks, epochs, and delegates genesis accounts
///
#pragma once
#include <logos/consensus/consensus_manager_config.hpp>
#include <logos/microblock/microblock.hpp>
#include <logos/epoch/epoch.hpp>
#include <logos/lib/numbers.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

namespace logos {
    class node_config;
    class block_store;
    class transaction;
}

static constexpr uint8_t NON_DELEGATE = 0xff;

enum class EpochDelegates {
    Current,
    Next
};

class NodeIdentityManager
{
    using Store     = logos::block_store;
    using Config    = ConsensusManagerConfig;
    using Log       = boost::log::sources::logger_mt;
    using IPs       = std::map<logos::account, std::string>;
    using Accounts  = logos::account[NUM_DELEGATES];

public:
    /// Class constructor
    /// @param store logos block store reference
    /// @param config node configuration reference
    NodeIdentityManager(Store&, const Config&);

    ~NodeIdentityManager() = default;

    /// Create genesis Epoch's and MicroBlock's
    void CreateGenesisBlocks(logos::transaction &transaction);

    /// Initialize genesis accounts
    /// @param transaction database transaction reference
    void CreateGenesisAccounts(logos::transaction &);

    /// Load genesis accounts
    void LoadGenesisAccounts();

    /// Initialize Genesis blocks/accounts
    /// @param config node configuration reference
    void Init(const Config &config);

    /// Identify this delegate and group of delegates in this epoch
    /// @param epoch identify for current or next epoch [in]
    /// @param idx this delegates index, NON_DELEGATE if not in the delegates list [out]
    /// @param delegates list of delegates in the requested epoch [out]
    void IdentifyDelegates(EpochDelegates epoch, uint8_t & idx, Accounts & delegates);
    /// Convenience function overload
    void IdentifyDelegates(EpochDelegates epoch, uint8_t & idx);

    Store &                 _store;                ///< logos block store reference
    static logos::account   _delegate_account;     ///< this delegate's account or 0 if non-delegate
    static IPs              _delegates_ip;         ///< all delegates ip
    static uint8_t          _global_delegate_idx;  ///< global delegate index in all delegate's list
    static bool             _run_local;            ///< run nodes locally
    Log                     _log;                  ///< boost log instances
};