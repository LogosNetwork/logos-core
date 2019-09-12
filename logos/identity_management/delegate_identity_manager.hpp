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
#include <logos/identity_management/sleeve.hpp>
#include <logos/lib/epoch_time_util.hpp>
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
    class NodeInterface;
}

static constexpr uint8_t NON_DELEGATE = 0xff;
constexpr int MAX_NTP_RETRIES = 20;

enum class QueriedEpoch {
    Current,
    Next
};

class PeerBinder;
class ConsensusContainer;

class GenesisBlock
{
public:
    GenesisBlock();
    ~GenesisBlock() = default;

    // Deserialize config file
    bool deserialize_json (bool &, boost::property_tree::ptree &);

    // For logs only, remove after
    void Sign(AccountPrivKey const & priv, AccountPubKey const & pub);

    // Verify signing of GenesisBlock by genesis account
    bool VerifySignature(AccountPubKey const & pub) const;

    // Check that requests match in config file
    bool Validate(logos::process_return & result) const;

    Log _log;
    Send gen_sends[NUM_DELEGATES*2];
    logos::genesis_delegate gen_delegates[NUM_DELEGATES*2];
    ApprovedMB gen_micro [3];
    ApprovedEB gen_epoch [3];
    StartRepresenting start [NUM_DELEGATES*2];
    AnnounceCandidacy announce [NUM_DELEGATES*2];
    CandidateInfo candidate [NUM_DELEGATES*2];
    AccountSig signature;
    BlockHash digest;
};

class DelegateIdentityManager : public std::enable_shared_from_this<DelegateIdentityManager>
{
    struct activation_schedule {
        activation_schedule() = default;
        activation_schedule(uint32_t const & epoch, bool const & activate) : start_epoch(epoch), activate(activate) {}
        uint32_t start_epoch = 0;  ///< epoch starting from the beginning of which the activation change begins taking effect
        bool     activate = false; ///< set true to activate, false to deactivate; ignored if start_epoch is old
    };
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
    using BLSKeyPairPtr
                    = std::unique_ptr<bls::KeyPair>;
    using ECIESKeyPairPtr
                    = std::unique_ptr<ECIESKeyPair>;
    using AddressAdKey
                    = std::pair<uint32_t, uint8_t>;
    using AddressAdList
                    = std::map<AddressAdKey, address_ad>;
    using AddressAdTxAList
                    = std::multimap<AddressAdKey, address_ad_txa>;
    using Timer     = boost::asio::deadline_timer;
    using DelegateIdCache = std::map<uint32_t, uint8_t>;

    friend class ConsensusContainer;

public:
    /// Class constructor
    /// @param node interface reference
    /// @param store reference to block store
    /// @param service reference to
    DelegateIdentityManager(logos::NodeInterface &node,
                            logos::block_store &store,
                            boost::asio::io_service &service,
                            class Sleeve &sleeve);

    ~DelegateIdentityManager()
    {
        LOG_DEBUG(_log) << "~DelegateIdentityManager()";
        CancelAdvert();
        // TODO: these class members should be changed to non-static
        _global_delegate_idx = NON_DELEGATE;
        _epoch_transition_enabled = true;
        _ecies_key = nullptr;
        _bls_key = nullptr;
    }

    /// Create genesis Epoch's and MicroBlock's
    void CreateGenesisBlocks(logos::transaction &transaction, GenesisBlock &config);

    /// Initialize genesis accounts
    /// @param transaction database transaction reference
    void CreateGenesisAccounts(logos::transaction &, GenesisBlock const &config);

    /// Load genesis accounts
    void LoadGenesisAccounts(GenesisBlock const &config);

    /// Initialize Genesis blocks/accounts
    void Init();

    /// Helper function to check if the node is Sleeved
    ///
    /// The caller of this function is responsible for acquiring `_activation_mutex`!
    /// @return true if Sleeved, false otherwise
    bool IsSleeved();

    /// Attempt to unlock Sleeve with provided password. If the Sleeve is successfully unlocked and
    /// already contains governance keys, IM will load the keys in memory and additionally coordinate
    /// consensus activation.
    ///
    ///     @param[in] password (in plaintext)
    /// @return sleeve command status
    sleeve_status UnlockSleeve(std::string const &);

    /// Attempt to lock the Sleeve (clearing its stored password). If successful, IM will also
    /// clear its stored keys from memory, as well as tear down any ongoing consensus components.
    ///
    /// @return sleeve command status
    sleeve_status LockSleeve();

    /// Store governance keys in the Sleeve, if Sleeve is unlocked, and start consensus / advertisement if activated
    ///
    ///     This method is called by logos::rpc_handler::sleeve_store_keys
    ///     @param[in] BLS private key in byte array
    ///     @param[in] ECIES private key in byte array
    ///     @param[in] boolean indicator of whether to overwrite existing stored content
    /// @return sleeve command status
    sleeve_status Sleeve(PlainText const &, PlainText const &, bool overwrite = false);

    /// Unsleeve, erasing any stored keys, tearing down consensus and cancelling advertisements
    ///
    /// @return sleeve command status
    sleeve_status Unsleeve();

    /// Reset the Sleeve (clearing everything stored). IM will also
    /// clear its stored keys from memory, as well as tear down any ongoing consensus components.
    ///
    /// @return sleeve command status
    void ResetSleeve();

    /// Check if any future activation change is scheduled
    ///
    /// @return true if scheduled, false otherwise.
    bool IsSettingChangeScheduled();

    /// Change activation status and scheduling
    /// Caller is responsible for acquiring _activation_mutex lock!
    ///
    ///     @param[in] whether to activate. true for activate, false for deactivate
    ///     @param[in] epoch number starting from which the activation status will take effect. 0 for immediate change
    /// @return sleeve command status
    sleeve_status ChangeActivation(bool const &, uint32_t const &);

    /// Attempt to cancel scheduled activation changes.
    /// If a deactivation scheduled for the next epoch is cancelled,
    /// IM will also advertise endpoints. Note that during the period of [ETES, ES),
    /// any scheduled activation changes for the immediately upcoming epoch cannot be cancelled.
    sleeve_status CancelActivationScheduling();

    /// Helper function to indicate whether the node, if sleeved, should be active in the next epoch
    ///
    /// Caller of the function is responsible for acquiring _activation_mutex lock!
    ///     @param[in] Lookup indicator of whether we are querying for the current or the next epoch
    /// @return true if both Sleeved and Activated during the queried epoch, false otherwise
    bool IsActiveInEpoch(QueriedEpoch queried_epoch);

    /// Apply scheduled activation, if appropriate.
    /// This should only be called by `ConsensusContainer::EpochTransitionEventsStart()`!
    /// Calling it any other time than right after current epoch number is incremented would lead to unexpected behavior
    void ApplyActivationSchedule();

    /// (1 of 2 overloads)
    /// Get the activation status summary.
    ///
    /// @return pair where the first element is whether IM is activated in current epoch,
    /// and the second the activation schedule. Note that an activation schedule with any
    /// start epoch **not greater than** the current epoch is considered not scheduled.
    std::pair<bool, activation_schedule> GetActivationStatus()
    {
        return std::make_pair(_activated[QueriedEpoch::Current], _activation_schedule);
    }

    /// (2 of 2 overloads)
    /// Get the activation status for the queried epoch (current or next).
    ///
    ///     @param[in] Epoch for which the activation status will be returned
    /// @return true if activated in the queried epoch, false otherwise
    bool GetActivationStatus(QueriedEpoch queried_epoch) { return _activated[queried_epoch]; }

    /// Identify this delegate and group of delegates in this epoch
    /// Caller is responsible for acquiring _activation_mutex!
    /// @param epoch identify for current or next epoch [in]
    /// @param idx this delegates index, NON_DELEGATE if not in the delegates list [out]
    /// @param eb approved epoch block [out]
    void IdentifyDelegates(QueriedEpoch queried_epoch, uint8_t & idx, ApprovedEBPtr &eb);
    /// Convenience function overload
    void IdentifyDelegates(QueriedEpoch queried_epoch, uint8_t & idx);

    /// Identify this delegate and group of delegates in this epoch
    /// Caller is responsible for acquiring _activation_mutex!
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
    /// @param queried_epoch current/next epoch
    /// @param ip txacceptor ip
    /// @param port txacceptor port
    /// @param bin_port txacceptor port to accept binary request
    /// @param json_port txacceptor port to accept json request
    /// @param add if true then add txacceptor, otherwise delete
    bool OnTxAcceptorUpdate(QueriedEpoch queried_epoch,
                            std::string &ip,
                            uint16_t port,
                            uint16_t bin_port,
                            uint16_t json_port,
                            bool add);

    /// Validate remote delegate's tx acceptor handshake message
    /// Used by standalone tx acceptor
    /// @param socket connected delegate's socket
    /// @param cb call back handler
    static void TxAValidateDelegate(std::shared_ptr<Socket> socket,
                                    const bls::PublicKey &bls_pub,
                                    std::function<void(bool result, const char *error)> cb);

    /// Get advertisement message to p2p app type
    /// @return P2pAppType
    template<typename Ad>
    static
    P2pAppType GetP2pAppType();

    /// Accurate way of checking if we are at a "stale" epoch,
    ///     i.e., if the previous epoch's block hasn't been post-committed in the database yet.
    ///     This is determined by checking if the most recent database epoch is just one behind current epoch
    /// @param[in] reference to most recent epoch in database
    /// @returns true if epoch reference is one behind current epoch number, false otherwise.
    static bool StaleEpoch(ApprovedEB & epoch);

    /// Crude way of checking if we are at a "stale" epoch.
    ///     This is determined by checking if current time is between transition start and last microblock proposal time
    /// @returns true if we suspect the previous epoch's block hasn't been post-committed in the database yet
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

    /// @returns this delegate global index into the logos::genesis_delegates
    static int GetGlobalDelegateIdx()  // TODO: limit usage to genesis initialization only
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

    void CancelAdvert()
    {
        LOG_DEBUG(_log) << "DelegateIdentiityManager::CancelAdvert - Cancelling advertisement timer.";
        std::lock_guard<std::mutex> lock(_ad_timer_mutex);
        _timer.cancel();
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

    void AdvertiseAndUpdateDB(const uint32_t &, const uint8_t &, std::shared_ptr<ApprovedEB>);

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

    void ServerHandshake(
            std::shared_ptr<Socket> socket,
            PeerBinder & binder,
            std::function<void(std::shared_ptr<AddressAd>)>);

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
        if (!_bls_key)
        {
            Log log;
            LOG_ERROR(log) << "DelegateIdentityManager::Sign - Not Sleeved.";
            return;
        }
        MessageValidator::Sign(hash, sig, [](bls::Signature &sig_real, const std::string &hash_str)
        {
            _bls_key->prv.sign(sig_real, hash_str);
        });
    }

    /// Get this delegate bls public key
    /// @returns bls public key
    static DelegatePubKey BlsPublicKey()
    {
        if (!_bls_key)
        {
            Log log;
            LOG_ERROR(log) << "DelegateIdentityManager::BlsPublicKey - Not Sleeved.";
            return DelegatePubKey();
        }
        return MessageValidator::BlsPublicKey(_bls_key->pub);
    }

private:

    static constexpr uint8_t INVALID_EPOCH_GAP = 10; ///< Gap client connections with epoch number greater than which plus our current epoch number will be rejected
    static constexpr Minutes AD_TIMEOUT_1{50};
    static constexpr Minutes AD_TIMEOUT_2{20};
    static constexpr Seconds TIMEOUT_SPREAD{1200};

    /// Start consensus / advertisement if activated
    ///
    ///     This method is called internally by Sleeve() and Unlock
    /// @param[in] LMDB transaction wrapper
    void OnSleeved(logos::transaction const &);

    /// Tear down consensus and cancel advertising
    void OnUnsleeved();

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

    /// Load delegate endpoint ads to self
    void LoadDBAd2Self();

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
    template<typename T>
    Seconds GetRandAdTime(const T &t)
    {
        if (TIMEOUT_SPREAD == Seconds(0)) return TConvert<Seconds>(t);
        return Seconds(rand() % TIMEOUT_SPREAD.count()) + TConvert<Seconds>(t);
    }

    static bool                 _epoch_transition_enabled; ///< is epoch transition enabled
    static uint8_t              _global_delegate_idx;  ///< global delegate index in all delegate's list
    /// Keys are retrieved from the wallet
    static ECIESKeyPairPtr      _ecies_key;            ///< this delegate's ecies key pair for ip encr/decr
    static BLSKeyPairPtr        _bls_key;              ///< bls key
    Store &                     _store;                ///< logos block store reference
    Log                         _log;                  ///< boost log instances
    ValidatorBuilder            _validator_builder;    ///< validator builder
    AddressAdList               _address_ad;           ///< list of delegates advertisement messages
    AddressAdTxAList            _address_ad_txa;       ///< list of delegates tx acceptor advertisement messages
    std::mutex                  _ad_mutex;             ///< protect address ad/txa lists
    Timer                       _timer;                ///< time for delegate/txacceptor advertisement
    std::mutex                  _ad_timer_mutex;       ///< mutex for protecting async timer
    logos::NodeInterface &      _node;                 ///< reference to logos node interface
    DelegateIdCache             _idx_cache;            ///< epoch to this delegate id map
    std::mutex                  _cache_mutex;
    const uint8_t               MAX_CACHE_SIZE = 2;
    class Sleeve &              _sleeve;               ///< Sleeve instance reference
    activation_schedule         _activation_schedule;  ///< activation schedule
    umap<QueriedEpoch, bool>    _activated;            ///< keeps track of activation status for both current and next epochs
    std::mutex                  _activation_mutex;     ///< mutex for protecting activation settings and timer
};

using namespace std;

/**
*  A Network Time Protocol Client that queries the DateTime from the Time Server located at hostname
*/
class NTPClient {

    private:
        string               _host_name;
        unsigned short       _port;

        std::atomic<time_t>  _ntp_time; // NTP time (in UNIX Format).
        std::atomic<time_t>  _delay;    // Delay.

        // RequestDatetime_UNIX_s:
        // Requests the date time in UNIX format.
        // static, meant to be called from a thread.
        static time_t RequestDatetime_UNIX_s(NTPClient *this_l);

        // timeout_s:
        // Static, this is the timeout thread which will timeout
        // if we don't get an ntp response fast enough.
        // meant to be called from a thread.
        static void timeout_s(NTPClient *this_l);

        // start_s:
        // Starts the process of getting ntp time every hour.
        // meant to be called from a thread.
        static void start_s(NTPClient *this_l);

    public:

        static constexpr int MAX_TIMEOUT = 10; // 5 seconds, user configurable.

        inline
        time_t getNtpTime() const
        {
            return _ntp_time;
        }

        inline
        time_t getDelay() const
        {
            return _delay;
        }

        inline
        void setNtpTime(time_t ntp)
        {
            _ntp_time = ntp;
        }

        inline
        void setDelay(time_t delay)
        {
            _delay = delay;
        }

        // **********************
        // **** Start of API ****
        // **********************

        // CTOR
        NTPClient(string i_hostname);

        // RequestDatetime_UNIX:
        // Requests the date time in UNIX format.
        // non-static, non-async
        time_t RequestDatetime_UNIX();

        // init:
        // Start async requests for ntp time
        // runs a loop in a seperate thread obtaining the ntp time
        // every hour. returns delta() on initial run.
        time_t init();

        // computeDelta:
        // Compute delta as difference between local time and ntp time.
        time_t computeDelta();

        // getCurrentDelta:
        // Delta from previous calculation
        time_t getCurrentDelta();

        // now:
        // The time now including the difference from delta.
        time_t now();

        // to_string:
        // Converts ntp time to a readable string based on format.
        inline
        std::string to_string(const char * format = "%a %b %d %Y %T", time_t t = 0) const
        {
            char date[128]={0};
            struct tm lt={0};
            localtime_r(&t,&lt);
            strftime(date,sizeof(date),format,&lt);
            return std::string(date);
        }

        // asyncNTP:
        // Makes an async request to get the ntp time,
        // if the timeout expires, it returns with time set to 0.
        // Otherwise, time is set to ntp time.
        void asyncNTP();

        // getTime:
        // Returns the ntp time.
        time_t getTime();

        // timedOut:
        // Returns true if we timed out (indicated by 0 value for ntp time)
        bool timedOut();

        // getDefault:
        // Returns current local time.
        time_t getDefault();
};

