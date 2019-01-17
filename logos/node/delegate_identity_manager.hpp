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
#include <logos/lib/log.hpp>

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

class DelegateIdentityManager
{
    using Store     = logos::block_store;
    using Config    = ConsensusManagerConfig;
    using IPs       = std::map<AccountAddress, std::string>;
    using Accounts  = AccountAddress[NUM_DELEGATES];

public:
    /// Class constructor
    /// @param store logos block store reference
    /// @param config node configuration reference
    DelegateIdentityManager(Store&, const Config&);

    ~DelegateIdentityManager() = default;

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

    /// Identify this delegate and group of delegates in this epoch
    /// @param epoch number [in]
    /// @param idx this delegates index, NON_DELEGATE if not in the delegates list [out]
    /// @param delegates list of delegates in the requested epoch [out]
    /// @return true if the epoch is found, false otherwise
    bool IdentifyDelegates(uint epoch, uint8_t & idx, Accounts & delegates);

    /// @returns true if current time is between transition start and last microblock proposal time
    static bool StaleEpoch();

    /// Get current epoch (i - 2)
    /// @param store block store reference
    /// @param epoch returned epoch
    static void GetCurrentEpoch(logos::block_store &store,  ApprovedEB & epoch);

    /// @returns true if epoch transition is enabled
    static bool IsEpochTransitionEnabled()
    {
        return _epoch_transition_enabled;
    }

    Store &                 _store;                ///< logos block store reference
    static AccountAddress   _delegate_account;     ///< this delegate's account or 0 if non-delegate
    static IPs              _delegates_ip;         ///< all delegates ip
    static uint8_t          _global_delegate_idx;  ///< global delegate index in all delegate's list
    Log                     _log;                  ///< boost log instances
    static bool             _epoch_transition_enabled; ///< is epoch transition enabled
};
