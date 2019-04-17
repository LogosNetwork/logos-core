/// @file
/// This file contains the declaration of the NodeIdentityManager class, which encapsulates
/// node identity management logic. Currently it holds all delegates ip, accounts, and delegate's index (formerly id)
/// into epoch's voted delegates. It also creates genesis microblocks, epochs, and delegates genesis accounts
///
#pragma once
#include <logos/consensus/persistence/validator_builder.hpp>
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
    class alarm;
}
class p2p_interface;

static constexpr uint8_t NON_DELEGATE = 0xff;

enum class EpochDelegates {
    Current,
    Next
};

class DelegateIdentityManager
{
    struct address_ad {
        std::string ip;
        uint16_t port;
    };
    struct address_ad_txa {
        std::string ip;
        uint16_t bin_port;
        uint16_t json_port;
    };
    using Store     = logos::block_store;
    using Config    = logos::node_config;
    using IPs       = std::map<AccountAddress, std::string>;
    using Accounts  = AccountAddress[NUM_DELEGATES];
    using Alarm     = logos::alarm;
    using ApprovedEBPtr
                    = std::shared_ptr<ApprovedEB>;
    using BlsKeyPairPtr
                    = std::unique_ptr<bls::KeyPair>;
    using AddressAdKey
                    = std::pair<uint32_t, uint8_t>;
    using AddressAdList
                    = std::map<AddressAdKey, address_ad>;
    using AddressAdTxAList
                    = std::multimap<AddressAdKey, address_ad_txa>;

public:
    /// Class constructor
    /// @param store logos block store reference
    /// @param config node configuration reference
    DelegateIdentityManager(Alarm &alarm, Store&, const Config&, p2p_interface & p2p);

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
    /// @param eb approved epoch block [out]
    void IdentifyDelegates(EpochDelegates epoch, uint8_t & idx, ApprovedEBPtr &eb);
    /// Convenience function overload
    void IdentifyDelegates(EpochDelegates epoch, uint8_t & idx);

    /// Identify this delegate and group of delegates in this epoch
    /// @param epoch number [in]
    /// @param idx this delegates index, NON_DELEGATE if not in the delegates list [out]
    /// @param eb approved epoch block [out]
    /// @return true if the epoch is found, false otherwise
    bool IdentifyDelegates(uint32_t epoch, uint8_t & idx, ApprovedEBPtr &eb);
    bool IdentifyDelegates(uint32_t epoch, uint8_t & idx)
    {
        ApprovedEBPtr eb = 0;
        return IdentifyDelegates(epoch, idx, eb);
    }

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

    /// @returns this delegate account
    static AccountAddress GetDelegateAccount()
    {
        return _delegate_account;
    }

    /// @returns this delegate global index into the logos::genesis_delegates
    static int GetGlobalDelegateIdx()
    {
        return (int)_global_delegate_idx;
    }

    /// Advertise if delegate in current or next epoch
    /// @param current_epoch_number current epoch number
    /// @param idx returns this delegate id in the Current epoch
    /// @param epoch approved epoch block for the current epoch
    void CheckAdvertise(uint32_t current_epoch_number,
                        uint8_t & idx,
                        ApprovedEBPtr &epoch);

    /// Advertise delegates ip
    /// @param epoch_number to advertise for
    /// @param delegate_id of the advertiser
    /// @param epoch contains the list of other delegates ecies pub key
    /// @param ids delegate ids to advertise to
    void Advertise(uint32_t epoch_number,
                   uint8_t delegate_id,
                   std::shared_ptr<ApprovedEB> epoch,
                   const std::vector<uint8_t>& ids);

    /// Propagate advertisement message via p2p
    /// @param epoch_number to advertise for
    /// @param delegate_id to advertise to (0xff if tx acceptor)
    /// @param buf serialized message
    void P2pPropagate(uint32_t epoch_number,
                      uint8_t delegate_id,
                      std::shared_ptr<std::vector<uint8_t>> buf);

    /// Handle received AddressAd message
    /// @param data serialized message
    /// @param size size of the serialized message
    /// @param prequel of the ad message [in]
    /// @param ip delegate's ip [out]
    /// @param port delegate's port [out]
    /// @returns true if the message is valid
    bool OnAddressAd(uint8_t *data,
                     size_t size,
                     const PrequelAddressAd &prequel,
                     std::string &ip,
                     uint16_t &port);

    /// Handle received AddressAdTxAcceptor message
    /// @param data serialized message
    /// @param size size of the serialized message
    /// @returns true if the message is valid
    bool OnAddressAdTxAcceptor(uint8_t *data, size_t size);

    /// Decrypt cyphertext
    /// @param cyphertext to decrypt
    /// @param buf decrypted message
    /// @param size of decrypted message
    static void Decrypt(const std::string &cyphertext, uint8_t *buf, size_t size);

    /// Get delegate's ip
    /// @param account delegate's account
    /// @returns ip
    static std::string GetDelegateIP(const AccountAddress &account)
    {
        assert (_delegates_ip.find(account) != _delegates_ip.end());
        return _delegates_ip[account];
    }

    /// Get this delegate's ip
    /// @returns ip
    static std::string GetDelegateIP()
    {
        return _delegates_ip[_delegate_account];
    }

    /// Validate signature
    /// @param epoch_number epoch number
    /// @param ad advertised message
    /// @returns true if validated
    bool ValidateSignature(uint32_t epoch_number, const CommonAddressAd &ad);

    /// Sign with bls signature
    /// @param hash to sign
    /// @param sig hash's signature [out]
    static void Sign(const BlockHash &hash, DelegateSig &sig)
    {
        string hash_str(reinterpret_cast<const char*>(hash.data()), HASH_SIZE);

        bls::Signature sig_real;
        _bls_key->prv.sign(sig_real, hash_str);

        string sig_str;
        sig_real.serialize(sig_str);
        memcpy(&sig, sig_str.data(), CONSENSUS_SIG_SIZE);
    }

    /// Get this delegate bls public key
    /// @returns bls public key
    static DelegatePubKey BlsPublicKey()
    {
        std::string keystring;
        _bls_key->pub.serialize(keystring);

        DelegatePubKey pk;
        memcpy(pk.data(), keystring.data(), CONSENSUS_PUB_KEY_SIZE);

        return pk;
    }

    static constexpr int RETRY_PROPAGATE = 5; // retry propagation timeout on failure, seconds

private:

    /// Get id's of delegates for IP advertising
    /// @param delegate_id of the advertiser
    std::vector<uint8_t> GetDelegatesToAdvertise(uint8_t delegate_id);

    /// Sign advertised message
    /// @param epoch_number epoch number
    /// @param ad message to sign
    void Sign(uint32_t epoch_number, CommonAddressAd &ad);

    /// Make Ad message and propagate it via p2p
    /// @param app_type of the message
    /// @param f serializer of the message
    /// @param epoch_number epoch number
    /// @param delegate_id delegate id
    /// @param args variable arguments
    template<typename Ad, typename SerializeF, typename ... Args>
    void MakeAdAndPropagate(P2pAppType app_type,
                            SerializeF &&f,
                            uint32_t epoch_number,
                            uint8_t delegate_id,
                            Args ... args);

    /// Update the AddressAd database
    /// @param prequel ad prequel
    /// @param data serialized ad message
    /// @param size size of the serialized message
    void UpdateDelegateAddressDB(const PrequelAddressAd &prequel, uint8_t *data, size_t size);

    /// Update the AddressAdTxAcceptor database
    /// @param prequel ad prequel
    /// @param data serialized ad message
    /// @param size size of the serialized message
    void UpdateTxAcceptorAddressDB(const PrequelAddressAd &prequel, uint8_t *data, size_t size);

    static bool             _epoch_transition_enabled; ///< is epoch transition enabled
    static AccountAddress   _delegate_account;     ///< this delegate's account or 0 if non-delegate
    static uint8_t          _global_delegate_idx;  ///< global delegate index in all delegate's list
    static IPs              _delegates_ip;         ///< all delegates ip
    /// TODO keys should be retrieved on start up from the wallet
    static ECIESKeyPair     _ecies_key;            ///< this delegate's ecies key pair for ip encr/decr
    static BlsKeyPairPtr    _bls_key;              ///< bls key
    Alarm &                 _alarm;                ///< logos alarm reference
    Store &                 _store;                ///< logos block store reference
    p2p_interface &         _p2p;                  ///< p2p interface reference
    const Config &          _config;               ///< consensus configuration
    Log                     _log;                  ///< boost log instances
    ValidatorBuilder        _validator_builder;    ///< validator builder
    AddressAdList           _address_ad;           ///< list of delegates advertisement messages
    AddressAdTxAList        _address_ad_txa;       ///< list of delegates tx acceptor advertisement messages
};
