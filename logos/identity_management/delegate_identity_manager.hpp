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
#include <boost/asio/deadline_timer.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/asio.hpp>

namespace logos {
    class node_config;
    class block_store;
    class transaction;
    class node;
}

static constexpr uint8_t NON_DELEGATE = 0xff;

enum class EpochDelegates {
    Current,
    Next
};

class DelegateIdentityManager
{
    struct address_ad {
        address_ad() = default;
        address_ad(std::string &ip, uint16_t port) : ip(ip), port(port) {}
        std::string ip;
        uint16_t port;
    };
    struct address_ad_txa {
        address_ad_txa() = default;
        address_ad_txa(std::string &ip, uint16_t bin_port, uint16_t json_port)
        : ip(ip)
        , bin_port(bin_port)
        , json_port(json_port) {}
        std::string ip;
        uint16_t bin_port;
        uint16_t json_port;
    };
    using Socket    = boost::asio::ip::tcp::socket;
    using ErrorCode = boost::system::error_code;
    using Store     = logos::block_store;
    using Config    = logos::node_config;
    using IPs       = std::map<AccountAddress, std::string>;
    using Accounts  = AccountAddress[NUM_DELEGATES];
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
    using Timer     = boost::asio::deadline_timer;
    using DelegateIdCache = std::map<uint32_t, uint8_t>;

public:
    /// Class constructor
    /// @param node reference
    DelegateIdentityManager(logos::node &node);

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

    /// Make delegate serialized advertisement message
    /// @param serialize serialization call back [in]
    /// @param isp2p is ad for p2p propagation [in]
    /// @param epoch_number epoch number [in]
    /// @param delegate_id delegage id [in]
    /// @return advertisement message
    template<typename Ad, typename SerializeF, typename ... Args>
    std::shared_ptr<std::vector<uint8_t>> MakeSerializedAd(SerializeF &&serialize,
                                                           bool isp2p,
                                                           uint32_t epoch_number,
                                                           uint8_t delegate_id,
                                                           Args ... args);

    /// Make delegate serialized AddressAd message
    /// @param store database reference [in]
    /// @param epoch_number epoch number [in]
    /// @param delegate_id delegage id [in]
    /// @param encr_delegateId encrypto delegate id [in]
    /// @param ip delegate's ip
    /// @param port delegate's port
    /// @return advertisement message
    std::shared_ptr<std::vector<uint8_t>> MakeSerializedAddressAd(uint32_t epoch_number,
                                                                  uint8_t delegate_id,
                                                                  uint8_t encr_delegate_id,
                                                                  const char *ip,
                                                                  uint16_t port);

    /// Delegate's tx acceptor handshake
    /// @param socket connection to txacceptor
    /// @param epoch_number epoch number
    /// @param delegate_id delegate id
    /// @param ip acceptor's ip
    /// @param port acceptor's binary port
    /// @param json_port acceptor's json port
    /// @param cb ad write call back
    void TxAcceptorHandshake(std::shared_ptr<Socket> socket,
                             uint32_t epoch_number,
                             uint8_t delegate_id,
                             const char *ip,
                             uint16_t port,
                             uint16_t json_port,
                             std::function<void(bool result)> cb);

    /// Handle rpc call to add/delete txacceptor
    /// @param epoch current/next epoch
    /// @param ip txacceptor ip
    /// @param port txacceptor port
    /// @param bin_port txacceptor port to accept binary request
    /// @param json_port txacceptor port to accept json request
    /// @param add if true then add txacceptor, otherwise delete
    bool OnTxAcceptorUpdate(EpochDelegates epoch,
                            std::string &ip,
                            uint16_t port,
                            uint16_t bin_port,
                            uint16_t json_port,
                            bool add);

    /// Validate tx acceptor handshake
    /// @param socket connected delegate's socket
    /// @param cb call back handler
    static void ValidateTxAcceptorConnection(std::shared_ptr<Socket> socket,
                                             const bls::PublicKey &bls_pub,
                                             std::function<void(bool result, const char *error)> cb);

    /// Get advertisement message to p2p app type
    /// @return P2pAppType
    template<typename Ad>
    static
    P2pAppType GetP2pAppType();

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

    static void EpochTransitionEnable(bool enable)
    {
        _epoch_transition_enabled = enable;
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
    /// @param advertise_current advertise current epoch
    /// @param idx returns this delegate id in the Current epoch
    /// @param epoch approved epoch block for the current epoch
    void CheckAdvertise(uint32_t current_epoch_number,
                        bool advertise_current,
                        uint8_t & idx,
                        ApprovedEBPtr &epoch);
    void CheckAdvertise(uint32_t current_epoch_number,
                        bool advertise_current)
    {
        uint8_t idx;
        ApprovedEBPtr eb;
        CheckAdvertise(current_epoch_number, advertise_current, idx, eb);
    }

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

    void ServerHandshake(std::shared_ptr<Socket> socket, std::function<void(std::shared_ptr<AddressAd>)>);

    void ClientHandshake(std::shared_ptr<Socket> socket,
                         uint32_t epoch_number,
                         uint8_t local_delegate_id,
                         uint8_t remote_delegate_id,
                         std::function<void(std::shared_ptr<AddressAd>)>);

    /// Update ad in-memory and database
    /// @param ad to update
    void UpdateAddressAd(const AddressAd &ad);

    /// Update this delegate's ad in-memory and database
    /// @param epoch_number epoch number
    /// @param delegate_id delegate id
    void UpdateAddressAd(uint32_t epoch_number, uint8_t delegate_id);

    /// Convert delegates' epoch number (block with elected delegates)
    /// to current epoch number. current is in the context of the delegates' epoch number;
    /// i.e. it's not necessary the true current epoch number
    /// @param epoch_number delegate's epoch number
    /// @returns current epoch number
    static uint32_t CurFromDelegatesEpoch(uint32_t epoch_number)
    {
        return epoch_number + 2;
    }

    /// Convert current epoch number to delegates' epoch number
    /// @param epoch_number current epoch number
    /// @returns delegates' epoch number
    static uint32_t CurToDelegatesEpoch(uint32_t epoch_number)
    {
        return epoch_number - 2;
    }

    /// Decrypt cyphertext
    /// @param cyphertext to decrypt
    /// @param buf decrypted message
    /// @param size of decrypted message
    static void Decrypt(const std::string &cyphertext, uint8_t *buf, size_t size);

    /// Get this delegate's ip
    /// @returns ip
    std::string GetDelegateIP(uint32_t epoch_number, uint8_t delegate_id)
    {
        std::lock_guard<std::mutex> lock(_ad_mutex);
        if (_address_ad.find({epoch_number, delegate_id}) != _address_ad.end())
        {
            return _address_ad[{epoch_number, delegate_id}].ip;
        }
            return "";
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
        MessageValidator::Sign(hash, sig, [](bls::Signature &sig_real, const std::string &hash_str)
        {
            _bls_key->prv.sign(sig_real, hash_str);
        });
    }

    /// Get this delegate bls public key
    /// @returns bls public key
    static DelegatePubKey BlsPublicKey()
    {
        return MessageValidator::BlsPublicKey(_bls_key->pub);
    }

private:

    static constexpr uint8_t INVALID_EPOCH_GAP = 10; ///< Gap client connections with epoch number greater than which plus our current epoch number will be rejected
    static constexpr std::chrono::minutes AD_TIMEOUT_1{50};
    static constexpr std::chrono::minutes AD_TIMEOUT_2{20};
    static constexpr std::chrono::minutes PEER_TIMEOUT{10};
    static constexpr std::chrono::seconds TIMEOUT_SPREAD{1200};

    void ReadAddressAd(std::shared_ptr<Socket> socket,
                       std::function<void(std::shared_ptr<AddressAd>)>);

    void WriteAddressAd(std::shared_ptr<Socket> socket,
                        uint32_t epoch_number,
                        uint8_t local_delegate_id,
                        uint8_t remote_delegate_id,
                        std::function<void(bool)>);

    /// Get id's of delegates for IP advertising
    /// @param delegate_id of the advertiser
    std::vector<uint8_t> GetDelegatesToAdvertise(uint8_t delegate_id);

    /// Sign advertised message
    /// @param epoch_number epoch number
    /// @param ad message to sign
    void Sign(uint32_t epoch_number, CommonAddressAd &ad);

    /// Make Ad message and propagate it via p2p
    /// @param f serializer of the message
    /// @param epoch_number epoch number
    /// @param delegate_id delegate id
    /// @param args variable arguments
    template<typename Ad, typename SerializeF, typename ... Args>
    void MakeAdAndPropagate(SerializeF &&f,
                            uint32_t epoch_number,
                            uint8_t delegate_id,
                            Args ... args);

    /// Update the AddressAd database
    /// @param prequel ad prequel
    /// @param data serialized ad message
    /// @param size size of the serialized message
    void UpdateAddressAdDB(const PrequelAddressAd &prequel, uint8_t *data, size_t size);

    /// Update the AddressAdTxAcceptor database
    /// @param ad message
    /// @param data serialized ad message
    /// @param size size of the serialized message
    void UpdateTxAcceptorAdDB(const AddressAdTxAcceptor &ad, uint8_t *data, size_t size);

    /// Load/clean up on start up ad information from DB
    void LoadDB();

    /// Schedule advertisement
    /// @param msec timeout value
    void ScheduleAd(boost::posix_time::milliseconds msec);
    /// Figure out advertisement time for the next epoch. Schedule if applicable.
    void ScheduleAd();

    /// Handle the ad timeout
    /// @param ec error code
    void Advert(const ErrorCode &ec);

    /// Return delegate's index in the current epoch
    /// @param cur_epoch_number current epoch number
    /// @returns delegate id
    uint8_t GetDelegateIdFromCache(uint32_t cur_epoch_number);

    /// Get random time for ad scheduling
    /// @param t base time
    /// @returns rand time
    std::chrono::seconds GetRandAdTime(const std::chrono::minutes &t)
    {
        return std::chrono::seconds(rand() % TIMEOUT_SPREAD.count()) + t;
    }

    static bool             _epoch_transition_enabled; ///< is epoch transition enabled
    static AccountAddress   _delegate_account;     ///< this delegate's account or 0 if non-delegate
    static uint8_t          _global_delegate_idx;  ///< global delegate index in all delegate's list
    /// TODO keys should be retrieved on start up from the wallet
    static ECIESKeyPair     _ecies_key;            ///< this delegate's ecies key pair for ip encr/decr
    static BlsKeyPairPtr    _bls_key;              ///< bls key
    Store &                 _store;                ///< logos block store reference
    Log                     _log;                  ///< boost log instances
    ValidatorBuilder        _validator_builder;    ///< validator builder
    AddressAdList           _address_ad;           ///< list of delegates advertisement messages
    AddressAdTxAList        _address_ad_txa;       ///< list of delegates tx acceptor advertisement messages
    std::mutex              _ad_mutex;             ///< protect address ad/txa lists
    Timer                   _timer;                ///< time for delegate/txacceptor advertisement
    logos::node &           _node;                 ///< logos node reference
    DelegateIdCache         _idx_cache;            ///< epoch to this delegate id map
    std::mutex              _cache_mutex;
    const uint8_t           MAX_CACHE_SIZE = 2;
};
