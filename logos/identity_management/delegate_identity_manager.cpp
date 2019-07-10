/// @file
/// This file contains the declaration of the DelegateIdentityManager class, which encapsulates
/// node identity management logic. Currently it holds all delegates ip, accounts, and delegate's index (formerly id)
/// into epoch's voted delegates. It also creates genesis microblocks, epochs, and delegates genesis accounts
///
#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
#include <logos/identity_management/keys.hpp>
#include <logos/epoch/recall_handler.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>
#include <logos/lib/ecies.hpp>
#include <logos/p2p/p2p.h>
#include <logos/staking/voting_power_manager.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

uint8_t DelegateIdentityManager::_global_delegate_idx = 0;
AccountAddress DelegateIdentityManager::_delegate_account = 0;
bool DelegateIdentityManager::_epoch_transition_enabled = true;
ECIESKeyPair DelegateIdentityManager::_ecies_key{};
std::unique_ptr<bls::KeyPair> DelegateIdentityManager::_bls_key = nullptr;
constexpr uint8_t DelegateIdentityManager::INVALID_EPOCH_GAP;
constexpr std::chrono::minutes DelegateIdentityManager::AD_TIMEOUT_1;
constexpr std::chrono::minutes DelegateIdentityManager::AD_TIMEOUT_2;
constexpr std::chrono::minutes DelegateIdentityManager::PEER_TIMEOUT;
constexpr std::chrono::seconds DelegateIdentityManager::TIMEOUT_SPREAD;

DelegateIdentityManager::DelegateIdentityManager(logos::node &node)
    : _store(node.store)
    , _validator_builder(_store)
    , _timer(node.alarm.service)
    , _node(node)
{
   _bls_key = std::make_unique<bls::KeyPair>();
   Init(node.config);
   LoadDB();
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE HARD-CODED PUB KEYS!!! TODO
void
DelegateIdentityManager::CreateGenesisBlocks(logos::transaction &transaction)
{
    BlockHash epoch_hash(0);
    BlockHash microblock_hash(0);
    MDB_txn *tx = transaction;
    // passed in block is overwritten
    auto update = [this, tx](auto msg, auto &block, const BlockHash &next, auto get, auto put) mutable->void{
        if (block.previous != 0)
        {
            if ((_store.*get)(block.previous, block, tx))
            {
                LOG_FATAL(_log) << "update failed to get previous " << msg << " "
                                << block.previous.to_string();
                trace_and_halt();
                return;
            }
            block.next = next;
            auto status = (_store.*put)(block, tx);
            if (status)
            {
                LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update the database";
                trace_and_halt();
            }
        }
    };
    for (int e = 0; e <= GENESIS_EPOCH; e++)
    {
        ApprovedEB epoch;
        ApprovedMB micro_block;
        micro_block.primary_delegate = 0xff;
        micro_block.epoch_number = e;
        micro_block.sequence = 0;
        micro_block.timestamp = 0;
        micro_block.previous = microblock_hash;
        micro_block.last_micro_block = 0;
        microblock_hash = micro_block.Hash();
        auto microblock_tip = micro_block.CreateTip();
        if (_store.micro_block_put(micro_block, transaction) ||
                _store.micro_block_tip_put(microblock_tip, transaction) )
        {
            LOG_FATAL(_log) << "update failed to insert micro_block or micro_block tip"
                            << microblock_hash.to_string();
            trace_and_halt();
            return;
        }

        update("micro block", micro_block, microblock_hash,
               &BlockStore::micro_block_get, &BlockStore::micro_block_put);

        const Amount initial_supply = 1000000000;

        epoch.primary_delegate = 0xff;
        epoch.epoch_number = e;
        epoch.sequence = 0;
        epoch.timestamp = 0;
        epoch.previous = epoch_hash;
        epoch.micro_block_tip = microblock_tip;
        epoch.total_supply = initial_supply;

        bls::KeyPair bls_key;
        ECIESPublicKey ecies_key;
        DelegatePubKey dpk;
        auto get_bls = [&bls_key, &dpk](const char *b)mutable->void {
            stringstream str(b);
            str >> bls_key.prv >> bls_key.pub;
            std::string s;
            bls_key.pub.serialize(s);
            assert(s.size() == CONSENSUS_PUB_KEY_SIZE);
            memcpy(dpk.data(), s.data(), CONSENSUS_PUB_KEY_SIZE);
        };
        auto get_ecies = [&ecies_key](const char *k)mutable->void {
            stringstream str(k);
            string pubk;
            string privk;
            str >> privk >> pubk;
            ecies_key.FromHexString(pubk);
        };
        for (uint8_t i = 0; i < NUM_DELEGATES*2; ++i) {
            uint8_t del = i;// + (e - 1) * 8 * _epoch_transition_enabled;
            get_bls(bls_keys[del]);
            get_ecies(ecies_keys[del]);
            char buff[5];
            sprintf(buff, "%02x", del + 1);
            logos::keypair pair(buff);
            Amount stake = 100000 + (uint64_t)del * 100;
            Delegate delegate = {pair.pub, dpk, ecies_key, 100000 + (uint64_t)del * 100, stake};
            if(e == 0)
            {
                //TODO: how to initialize the delegate accounts for elections
                //Every delegate should be a representative
                //In the proper case, all delegates have submitted a
                //StartRepresenting request, as well as AnnounceCandidacy
                //Should we add them to the candidate db so that way when
                //elections start they are candidates?
                RepInfo rep;
                //dummy request for epoch transition
                StartRepresenting start;
                start.epoch_num = 0;
                start.origin = pair.pub;
                start.stake = stake;
                rep.rep_action_tip = start.Hash();
                if (_store.request_put(start, transaction))
                {
                    LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update StartRepresenting";
                    trace_and_halt();
                }
                //dummy request for epoch transition
                AnnounceCandidacy announce;
                announce.epoch_num = 0;
                announce.origin = pair.pub;
                announce.stake = stake;
                announce.bls_key = dpk;
                announce.ecies_key = ecies_key;
                rep.candidacy_action_tip = announce.Hash();
                if (_store.request_put(announce, transaction) || _store.rep_put(pair.pub, rep, transaction))
                {
                    LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update AnnounceCandidacy";
                    trace_and_halt();
                }
                VotingPowerManager::GetInstance()->AddSelfStake(pair.pub,stake,0,transaction);
                CandidateInfo candidate;
                candidate.next_stake = stake;
                candidate.cur_stake = stake;
                candidate.bls_key = dpk;
                candidate.ecies_key = ecies_key;
                //TODO: should we put these accounts into candidate list even
                //though elections are not being held yet?
                if (_store.candidate_put(pair.pub, candidate, transaction))
                {
                    LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update CandidateInfo";
                    trace_and_halt();
                }
            }


            LOG_INFO(_log) << __func__ << "bls public key for delegate i=" << (int)i
                            << " pub_key=" << pair.pub.to_account();
            if(i < NUM_DELEGATES)
            {
                delegate.starting_term = false;
                epoch.delegates[i] = delegate;
            }
        }

        epoch_hash = epoch.Hash();
        if(_store.epoch_put(epoch, transaction) ||
                _store.epoch_tip_put(epoch.CreateTip(), transaction))
        {
            LOG_FATAL(_log) << "update failed to insert epoch or epoch tip"
                            << epoch_hash.to_string();
            trace_and_halt();
            return;
        }
        update("epoch", epoch, epoch_hash,
               &BlockStore::epoch_get, &BlockStore::epoch_put);
    }
}

void
DelegateIdentityManager::Init(const Config &config)
{
    logos::transaction transaction (_store.environment, nullptr, true);

    const ConsensusManagerConfig &cmconfig = _node.config.consensus_manager_config;
    _epoch_transition_enabled = cmconfig.enable_epoch_transition;

    EpochVotingManager::ENABLE_ELECTIONS = cmconfig.enable_elections;

    Tip epoch_tip;
    BlockHash &epoch_tip_hash = epoch_tip.digest;
    uint32_t epoch_number = 0;
    if (_store.epoch_tip_get(epoch_tip))
    {
        CreateGenesisBlocks(transaction);
        epoch_number = GENESIS_EPOCH + 1;
    }
    else
    {
        ApprovedEB previous_epoch;
        if (_store.epoch_get(epoch_tip_hash, previous_epoch))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::Init Failed to get epoch: " << epoch_tip.to_string();
            trace_and_halt();
        }

        // if a node starts after epoch transition start but before the last microblock
        // is proposed then the latest epoch block is not created yet and the epoch number
        // has to be increamented by 1
        epoch_number = previous_epoch.epoch_number + 1;
        epoch_number = (StaleEpoch()) ? epoch_number + 1 : epoch_number;
    }

    // check account_db
    if(_store.account_db_empty())
    {
        auto error (false);

        // Construct genesis open block
        boost::property_tree::ptree tree;
        std::stringstream istream(logos::logos_test_genesis);
        boost::property_tree::read_json(istream, tree);
        Send logos_genesis_block(error, tree);

        if(error)
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::Init - Failed to initialize Logos genesis block.";
            trace_and_halt();
        }

        //TODO check with Greg
        ReceiveBlock logos_genesis_receive(0, logos_genesis_block.GetHash(), 0);
        if (_store.request_put(logos_genesis_block, transaction) ||
            _store.receive_put(logos_genesis_receive.Hash(), logos_genesis_receive, transaction) ||
            _store.account_put(logos::genesis_account,
            {
                /* Head         */ logos_genesis_block.GetHash(),
                /* Receive Head */ logos_genesis_receive.Hash(),
                /* Rep          */ 0,
                /* Open         */ logos_genesis_block.GetHash(),
                /* Amount       */ logos_genesis_block.transactions[0].amount,
                /* Time         */ logos::seconds_since_epoch(),
                /* Count        */ 1,
                /* Receive      */ 1,
                /* Claim Epoch  */ 0
            },
            transaction))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::Init failed to update the database";
            trace_and_halt();
        }
        CreateGenesisAccounts(transaction);
    }
    else {LoadGenesisAccounts();}

    // TODO, wallet integration
    _delegate_account = logos::genesis_delegates[cmconfig.delegate_id].key.pub;
    _ecies_key = logos::genesis_delegates[cmconfig.delegate_id].ecies_key;
    *_bls_key = logos::genesis_delegates[cmconfig.delegate_id].bls_key;
    _global_delegate_idx = cmconfig.delegate_id;
    LOG_INFO(_log) << "delegate id is " << (int)_global_delegate_idx;

    ConsensusContainer::SetCurEpochNumber(epoch_number);
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE PRIVATE KEYS ARE 0-63!!! TBD
/// THE SAME GOES FOR BLS KEYS
void
DelegateIdentityManager::CreateGenesisAccounts(logos::transaction &transaction)
{
    logos::account_info genesis_account;
    if (_store.account_get(logos::logos_test_account, genesis_account, transaction))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisAccounts, failed to get account";
        trace_and_halt();
    }

    // create genesis delegate accounts
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        stringstream str(bls_keys[del]);
        bls::KeyPair bls_key;
        str >> bls_key.prv >> bls_key.pub;
        ECIESKeyPair ecies_key(ecies_keys[del]);
        logos::genesis_delegate delegate{logos::keypair(buff), bls_key, ecies_key,
                                         100000 + (uint64_t) del * 100, 100000 + (uint64_t) del * 100};
        logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);

        logos::amount amount((del + 1) * 1000000 * PersistenceManager<R>::MinTransactionFee(RequestType::Send));
        uint64_t work = 0;

        Send request(logos::logos_test_account,   // account
                     genesis_account.head,        // previous
                     genesis_account.block_count, // sequence
                     pair.pub,                    // link/to
                     amount,
                     0,                           // transaction fee
                     logos::test_genesis_key.prv.data, // SG: Sign with correct key
                     logos::test_genesis_key.pub,
                     work);

        genesis_account.SetBalance(genesis_account.GetBalance() - amount,0,transaction);
        genesis_account.head = request.GetHash();
        genesis_account.block_count++;
        genesis_account.modified = logos::seconds_since_epoch();

        ReceiveBlock receive(0, request.GetHash(), 0);

        if (_store.request_put(request, transaction) ||
            _store.receive_put(receive.Hash(), receive, transaction) ||
            _store.account_put(pair.pub,
                           {
                               /* Head          */ 0,
                               /* Receive       */ receive.Hash(),
                               /* Rep           */ 0,
                               /* Open          */ request.GetHash(),
                               /* Amount        */ amount,
                               /* Time          */ logos::seconds_since_epoch(),
                               /* Count         */ 0,
                               /* Receive Count */ 1,
                               /* Claim Epoch   */ 0
                           },
                           transaction))
        {
           LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisAccounts, failed to update the database";
           trace_and_halt();
        }
    }

    if (_store.account_put(logos::logos_test_account, genesis_account, transaction))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisAccounts, failed to update the account";
        trace_and_halt();
    }
}

void
DelegateIdentityManager::LoadGenesisAccounts()
{
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        stringstream str(bls_keys[del]);
        bls::KeyPair bls_key;
        str >> bls_key.prv >> bls_key.pub;
        ECIESKeyPair ecies_key(ecies_keys[del]);
        logos::genesis_delegate delegate{logos::keypair(buff), bls_key, ecies_key,
                                         100000 + (uint64_t) del * 100, 100000 + (uint64_t) del * 100};
        logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);
    }
}

void
DelegateIdentityManager::IdentifyDelegates(
    EpochDelegates epoch_delegates,
    uint8_t &delegate_idx)
{
    ApprovedEBPtr epoch;
    IdentifyDelegates(epoch_delegates, delegate_idx, epoch);
}

void
DelegateIdentityManager::IdentifyDelegates(
    EpochDelegates epoch_delegates,
    uint8_t &delegate_idx,
    ApprovedEBPtr &epoch)
{
    delegate_idx = NON_DELEGATE;

    bool stale_epoch = StaleEpoch();
    // requested epoch block is not created yet
    if (stale_epoch && epoch_delegates == EpochDelegates::Next)
    {
        LOG_ERROR(_log) << "DelegateIdentityManager::IdentifyDelegates delegates set is requested for next epoch but epoch is stale";
        return;
    }

    Tip epoch_tip;
    BlockHash & epoch_tip_hash = epoch_tip.digest;
    if (_store.epoch_tip_get(epoch_tip))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch tip";
        trace_and_halt();
    }

    epoch = std::make_shared<ApprovedEB>();
    if (_store.epoch_get(epoch_tip_hash, *epoch))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch: "
                        << epoch_tip.to_string();
        trace_and_halt();
    }

    if (!stale_epoch && epoch_delegates == EpochDelegates::Current)
    {
        if (_store.epoch_get(epoch->previous, *epoch))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get current delegate's epoch: "
                            << epoch->previous.to_string();
            trace_and_halt();
        }
    }

    LOG_DEBUG(_log) << "DelegateIdentityManager::IdentifyDelegates retrieving delegates from epoch "
                    << epoch->epoch_number;
    // Is this delegate included in the current/next epoch consensus?
    for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
    {
        // update delegates for the requested epoch
        if (epoch->delegates[del].account == _delegate_account)
        {
            delegate_idx = del;
            break;
        }
    }
}

bool
DelegateIdentityManager::IdentifyDelegates(
    uint32_t epoch_number,
    uint8_t &delegate_idx,
    ApprovedEBPtr & epoch)
{
	Tip tip;
    delegate_idx = NON_DELEGATE;
    BlockHash hash;
    if (_store.epoch_tip_get(tip))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch tip";
        trace_and_halt();
    }
    hash = tip.digest;

    epoch = std::make_shared<ApprovedEB>();

    auto get = [this](BlockHash &hash, ApprovedEBPtr epoch) {
        if (_store.epoch_get(hash, *epoch))
        {
            if (hash != 0) {
                LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch: "
                                << hash.to_string();
                trace_and_halt();
            }
            return false;
        }
        return true;
    };

    bool found = false;
    for (bool res = get(hash, epoch);
              res && !(found = epoch->epoch_number == epoch_number);
              res = get(hash, epoch))
    {
        hash = epoch->previous;
    }

    if (found)
    {
        // Is this delegate included in the current/next epoch consensus?
        delegate_idx = NON_DELEGATE;
        for (uint8_t del = 0; del < NUM_DELEGATES; ++del) {
            // update delegates for the requested epoch
            if (epoch->delegates[del].account == _delegate_account) {
                delegate_idx = del;
                break;
            }
        }
    }

    return found;
}

bool
DelegateIdentityManager::StaleEpoch()
{

    auto now_msec = GetStamp();
    auto rem = Seconds(now_msec % TConvert<Milliseconds>(EPOCH_PROPOSAL_TIME).count());
    return (rem < MICROBLOCK_PROPOSAL_TIME);
}

void
DelegateIdentityManager::GetCurrentEpoch(BlockStore &store, ApprovedEB &epoch)
{
    Tip tip;
    BlockHash &hash = tip.digest;

    if (store.epoch_tip_get(tip))
    {
        trace_and_halt();
    }

    if (store.epoch_get(hash, epoch))
    {
        trace_and_halt();
    }

    if (StaleEpoch())
    {
        return;
    }

    if (store.epoch_get(epoch.previous, epoch))
    {
        trace_and_halt();
    }
}

std::vector<uint8_t>
DelegateIdentityManager::GetDelegatesToAdvertise(uint8_t delegate_id)
{
    std::vector<uint8_t> ids;
    for (uint8_t del = 0; del < delegate_id; ++del)
    {
        ids.push_back(del);
    }
    return ids;
}

void
DelegateIdentityManager::CheckAdvertise(uint32_t current_epoch_number,
                                        bool advertise_current,
                                        uint8_t &idx,
                                        ApprovedEBPtr &epoch_current)
{
    ApprovedEBPtr epoch_next;

    LOG_DEBUG(_log) << "DelegateIdentityManager::CheckAdvertise for epoch " << current_epoch_number;

    // advertise for next epoch
    IdentifyDelegates(EpochDelegates::Next, idx, epoch_next);
    if (idx != NON_DELEGATE)
    {
        auto ids = GetDelegatesToAdvertise(idx);
        Advertise(current_epoch_number+1, idx, epoch_next, ids);
        UpdateAddressAd(current_epoch_number+1, idx);
    }

    // advertise for current epoch
    if (advertise_current)
    {
        IdentifyDelegates(EpochDelegates::Current, idx, epoch_current);
        if (idx != NON_DELEGATE) {
            auto ids = GetDelegatesToAdvertise(idx);
            Advertise(current_epoch_number, idx, epoch_current, ids);
            UpdateAddressAd(current_epoch_number, idx);
        }
    }

    ScheduleAd();
}

void
DelegateIdentityManager::P2pPropagate(
    uint32_t epoch_number,
    uint8_t delegate_id,
    std::shared_ptr<std::vector<uint8_t>> buf)
{
    bool res = _node.p2p.PropagateMessage(buf->data(), buf->size(), true);
    LOG_DEBUG(_log) << "DelegateIdentityManager::Advertise, " << (res?"propagating":"failed")
                    << ": epoch number " << epoch_number
                    << ", delegate id " << (int) delegate_id
                    << ", ip " << _node.config.consensus_manager_config.local_address
                    << ", port " << _node.config.consensus_manager_config.peer_port
                    << ", size " << buf->size();
}

void
DelegateIdentityManager::Sign(uint32_t epoch_number, CommonAddressAd &ad)
{
    auto validator = _validator_builder.GetValidator(epoch_number);
    auto hash = ad.Hash();
    validator->Sign(hash, ad.signature);
}

bool
DelegateIdentityManager::ValidateSignature(uint32_t epoch_number, const CommonAddressAd &ad)
{
    auto hash = ad.Hash();
    auto validator = _validator_builder.GetValidator(epoch_number);
    return validator->Validate(hash, ad.signature, ad.delegate_id);
}

template<>
P2pAppType
DelegateIdentityManager::GetP2pAppType<AddressAd>()
{
    return P2pAppType::AddressAd;
}

template<>
P2pAppType
DelegateIdentityManager::GetP2pAppType<AddressAdTxAcceptor>() {
    return P2pAppType::AddressAdTxAcceptor;
}

std::shared_ptr<std::vector<uint8_t>>
DelegateIdentityManager::MakeSerializedAddressAd(uint32_t epoch_number,
                                                 uint8_t delegate_id,
                                                 uint8_t encr_delegate_id,
                                                 const char *ip,
                                                 uint16_t port)
{
    uint8_t idx = 0xff;
    ApprovedEBPtr eb;
    IdentifyDelegates(CurToDelegatesEpoch(epoch_number), idx, eb);
    return MakeSerializedAd<AddressAd>([encr_delegate_id, eb](auto ad, logos::vectorstream &s)->size_t{
        return (*ad).Serialize(s, eb->delegates[encr_delegate_id].ecies_pub);
    }, false, epoch_number, delegate_id, encr_delegate_id, ip, port);
}

template<typename Ad, typename SerializeF, typename ... Args>
std::shared_ptr<std::vector<uint8_t>>
DelegateIdentityManager::MakeSerializedAd(SerializeF &&serialize,
                                          bool isp2p,
                                          uint32_t epoch_number,
                                          uint8_t delegate_id,
                                          Args ... args)
{
    size_t size = 0;
    auto buf = std::make_shared<std::vector<uint8_t>>();
    {
        logos::vectorstream stream(*buf);
        if (isp2p)
        {
            P2pHeader header(logos_version, GetP2pAppType<Ad>());
            size = header.Serialize(stream);
            assert(size == P2pHeader::SIZE);
        }

        Ad addressAd(epoch_number, delegate_id, args ... );
        Sign(epoch_number, addressAd);
        serialize(&addressAd, stream);
    }
    size = buf->size();
    assert(size >= Ad::SIZE);
    return buf;
}

template<typename Ad, typename SerializeF, typename ... Args>
void
DelegateIdentityManager::MakeAdAndPropagate(
    SerializeF &&serialize,
    uint32_t epoch_number,
    uint8_t delegate_id,
    Args ... args)
{
    auto buf = MakeSerializedAd<Ad>(serialize, true, epoch_number, delegate_id, args ...);
    P2pPropagate(epoch_number, delegate_id, buf);
}

void
DelegateIdentityManager::Advertise(
    uint32_t epoch_number,
    uint8_t delegate_id,
    std::shared_ptr<ApprovedEB> epoch,
    const std::vector<uint8_t> &ids)
{
    // Advertise to other delegats this delegate's ip
    const ConsensusManagerConfig & cmconfig = _node.config.consensus_manager_config;
    for (auto it = ids.begin(); it != ids.end(); ++it)
    {
        auto encr_delegate_id = *it;
        MakeAdAndPropagate<AddressAd>([&epoch, &encr_delegate_id](auto ad, logos::vectorstream &s)->size_t{
                                          return (*ad).Serialize(s, epoch->delegates[encr_delegate_id].ecies_pub);
                                      },
                                      epoch_number,
                                      delegate_id,
                                      encr_delegate_id,
                                      cmconfig.local_address.c_str(),
                                      cmconfig.peer_port);
    }

    // Advertise to all nodes this delegate's tx acceptors
    const TxAcceptorConfig &txconfig = _node.config.tx_acceptor_config;
    auto acceptors = txconfig.tx_acceptors;
    if (acceptors.empty())
    {
        acceptors.push_back({txconfig.acceptor_ip, txconfig.port});
    }
    for (auto txa : acceptors)
    {
        MakeAdAndPropagate<AddressAdTxAcceptor>([](auto ad, logos::vectorstream &s)->size_t{
                                                    return (*ad).Serialize(s);
                                                },
                                                epoch_number,
                                                delegate_id,
                                                txa.ip.c_str(),
                                                txconfig.bin_port,
                                                txconfig.json_port);
    }
}

void
DelegateIdentityManager::Decrypt(const std::string &cyphertext, uint8_t *buf, size_t size)
{
    _ecies_key.prv.Decrypt(cyphertext, buf, size);
}

bool
DelegateIdentityManager::OnAddressAd(uint8_t *data,
                                     size_t size,
                                     const PrequelAddressAd &prequel,
                                     std::string &ip,
                                     uint16_t &port)
{
    bool res = false;

    // don't update if already have it
    if (_address_ad.find({prequel.epoch_number, prequel.delegate_id}) != _address_ad.end())
    {
        return true;
    }

    LOG_DEBUG(_log) << "DelegateIdentityManager::OnAddressAd, epoch " << prequel.epoch_number
                    << " delegate id " << (int)prequel.delegate_id
                    << " encr delegate id " << (int)prequel.encr_delegate_id
                    << " size " << size;

    auto current_epoch_number = ConsensusContainer::GetCurEpochNumber();
    bool current_or_next = prequel.epoch_number == current_epoch_number ||
                           prequel.epoch_number == (current_epoch_number + 1);
    if (current_or_next)
    {
        uint8_t idx = GetDelegateIdFromCache(prequel.epoch_number);

        // ad is encrypted with this delegate's ecies public key
        if (prequel.encr_delegate_id == idx)
        {
            try {
                bool error = false;
                logos::bufferstream stream(data + PrequelAddressAd::SIZE, size - PrequelAddressAd::SIZE);
                AddressAd addressAd(error, prequel, stream, &DelegateIdentityManager::Decrypt);
                if (error) {
                    LOG_ERROR(_log) << "DelegateIdentityManager::OnAddressAd, failed to deserialize AddressAd";
                    return false;
                }
                if (!ValidateSignature(prequel.epoch_number, addressAd)) {
                    LOG_ERROR(_log) << "DelegateIdentityManager::OnAddressAd, failed to validate AddressAd signature";
                    return false;
                }

                ip = addressAd.GetIP();
                port = addressAd.port;

                {
                    std::lock_guard<std::mutex> lock(_ad_mutex);
                    _address_ad[{prequel.epoch_number, prequel.delegate_id}] = {ip, port};
                }
                res = true;

                LOG_DEBUG(_log) << "DelegateIdentityManager::OnAddressAd, epoch number " << addressAd.epoch_number
                                << ", delegate id " << (int)prequel.delegate_id
                                << ", ip " << ip
                                << ", port " << port;
            } catch (const std::exception &e)
            {
                LOG_ERROR(_log) << "DelegateIdentityManager::OnAddressAd, failed to decrypt AddressAd "
                                << " epoch number " << prequel.epoch_number
                                << " delegate id " << (int)prequel.delegate_id
                                << " encr delegate id " << (int)prequel.encr_delegate_id
                                << " size " << size
                                << " exception " << e.what();
                return false;
            }
        }

        UpdateAddressAdDB(prequel, data, size);
    }

    return res;
}

void
DelegateIdentityManager::UpdateAddressAdDB(const PrequelAddressAd &prequel, uint8_t *data, size_t size)
{
    logos::transaction transaction (_store.environment, nullptr, true);
    // update new
    if (_store.ad_put<logos::block_store::ad_key>(transaction,
                                                  data,
                                                  size,
                                                  prequel.epoch_number,
                                                  prequel.delegate_id,
                                                  prequel.encr_delegate_id))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::UpdateAddressAdDB, epoch number " << prequel.epoch_number
                        << " delegate id " << (int)prequel.delegate_id
                        << " encr delegate id " << (int)prequel.encr_delegate_id;
        trace_and_halt();
    }
    // delete old
    auto current_epoch_number = ConsensusContainer::GetCurEpochNumber();
    _store.ad_del<logos::block_store::ad_key>(transaction,
                                              current_epoch_number - 1,
                                              prequel.delegate_id,
                                              prequel.encr_delegate_id);
}

bool
DelegateIdentityManager::OnAddressAdTxAcceptor(uint8_t *data, size_t size)
{
    bool error = false;
    logos::bufferstream stream(data, size);
    PrequelAddressAd prequel(error, stream);
    if (error)
    {
        LOG_ERROR(_log) << "ConsensusContainer::OnAddressAdTxAcceptor, failed to deserialize PrequelAddressAd";
        return false;
    }

    // don't update if already have it
    if (_address_ad_txa.find({prequel.epoch_number, prequel.delegate_id}) != _address_ad_txa.end())
    {
        return true;
    }

    auto current_epoch_number = ConsensusContainer::GetCurEpochNumber();
    bool current_or_next = prequel.epoch_number == current_epoch_number || (current_epoch_number + 1);
    if (current_or_next)
    {
        error = false;
        AddressAdTxAcceptor addressAd(error, prequel, stream);
        if (error)
        {
            LOG_ERROR(_log) << "ConsensusContainer::OnAddressAdTxAcceptor, failed to deserialize AddressAdTxAcceptor";
            return false;
        }

        if (!ValidateSignature(prequel.epoch_number, addressAd))
        {
            LOG_ERROR(_log) << "ConsensusContainer::OnAddressAdTxAcceptor, failed to validate AddressAd signature";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(_ad_mutex);
            if (addressAd.add)
            {
                std::string ip = addressAd.GetIP();
                _address_ad_txa.emplace(std::piecewise_construct,
                                        std::forward_as_tuple(prequel.epoch_number, prequel.delegate_id),
                                        std::forward_as_tuple(ip, addressAd.port, addressAd.json_port));
            }
            else
            {
                _address_ad_txa.erase({prequel.epoch_number, prequel.delegate_id});
            }
        }

        LOG_DEBUG(_log) << "ConsensusContainer::OnAddressAdTxAcceptor, ip " << addressAd.GetIP()
                        << ", port " << addressAd.port
                        << ", json port " << addressAd.json_port;

        UpdateTxAcceptorAdDB(addressAd, data, size);
    }

    return true;
}

void
DelegateIdentityManager::UpdateTxAcceptorAdDB(const AddressAdTxAcceptor &ad, uint8_t *data, size_t size)
{
    logos::transaction transaction (_store.environment, nullptr, true);

    if (!ad.add)
    {
        _store.ad_del<logos::block_store::ad_txa_key>(transaction,
                                                      ad.epoch_number,
                                                      ad.delegate_id);
        return;
    }

    // update new
    if (_store.ad_put<logos::block_store::ad_txa_key>(transaction,
                                                      data,
                                                      size,
                                                      ad.epoch_number,
                                                      ad.delegate_id))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::UpdateTxAcceptorAdDB, epoch number " << ad.epoch_number
                        << " delegate id " << (int)ad.delegate_id;
        trace_and_halt();
    }
    // delete old
    auto current_epoch_number = ConsensusContainer::GetCurEpochNumber();
    _store.ad_del<logos::block_store::ad_txa_key>(transaction,
                                                  current_epoch_number - 1,
                                                  ad.delegate_id);
}

/// The server reads client's ad and responds with it's own ad if the client's ad is valid
/// The server can still disconnect. One possible use case is during epoch transition.
/// Due to the clock drift a client can transition to Connect state while the server
/// has not transitioned to Connect state yet. In this case the server closes the connection
/// and client will attempt reconnecting five seconds later.
void
DelegateIdentityManager::ServerHandshake(std::shared_ptr<Socket> socket,
                                         std::function<void(std::shared_ptr<AddressAd>)> cb)
{
    ReadAddressAd(socket, [this, socket, cb](std::shared_ptr<AddressAd> ad){
        if (!ad)
        {
            LOG_DEBUG(_log) << "DelegateIdentityManager::ServerHandshake failed to read client's ad";
            cb(nullptr);
            return;
        }
        WriteAddressAd(socket,
                       ad->epoch_number,
                       ad->encr_delegate_id,
                       ad->delegate_id,
                       [this, socket, ad, cb](bool result) {
            if (result)
            {
                cb(ad);
            }
            else
            {
                cb(nullptr);
            }
        });
    });
}

/// Client writes its ad to the server and then reads server's ad.
void
DelegateIdentityManager::ClientHandshake(std::shared_ptr<Socket> socket,
                                         uint32_t epoch_number,
                                         uint8_t local_delegate_id,
                                         uint8_t remote_delegate_id,
                                         std::function<void(std::shared_ptr<AddressAd>)> cb)
{
    WriteAddressAd(socket, epoch_number, local_delegate_id, remote_delegate_id, [this, socket, cb](bool result) {
        if (result)
        {
            ReadAddressAd(socket, [this, socket, cb](std::shared_ptr<AddressAd> ad) {
                cb(ad);
            });
        }
        else
        {
            cb(nullptr);
        }
    });
}

void
DelegateIdentityManager::WriteAddressAd(std::shared_ptr<Socket> socket,
                                        uint32_t epoch_number,
                                        uint8_t local_delegate_id,
                                        uint8_t remote_delegate_id,
                                        std::function<void(bool)> cb)
{
    auto & config = _node.config.consensus_manager_config;
    auto buf = MakeSerializedAddressAd(epoch_number,
                                       local_delegate_id,
                                       remote_delegate_id,
                                       config.local_address.c_str(),
                                       config.peer_port);
    boost::asio::async_write(*socket,
                             boost::asio::buffer(buf->data(), buf->size()),
                             [this, socket, buf, cb](const ErrorCode &ec, size_t size){
        if (ec)
        {
            LOG_ERROR(_log) << "DelegateIdentityManager::WriteAddressAd write error " << ec.message();
            cb(false);
        }
        else
        {
            LOG_DEBUG(_log) << "DelegateIdentityManager::WriteAddressAd wrote ad, size " << buf->size();
            cb(true);
        }
    });
}

void
DelegateIdentityManager::ReadAddressAd(std::shared_ptr<Socket> socket,
                                       std::function<void(std::shared_ptr<AddressAd>)> cb)
{
    auto buf = std::make_shared<std::array<uint8_t, PrequelAddressAd::SIZE>>();

    // Read prequel to get the payload size
    boost::asio::async_read(*socket,
                           boost::asio::buffer(buf->data(), buf->size()),
                           [this, socket, buf, cb](const ErrorCode &ec, size_t size) {
        if (ec)
        {
            LOG_ERROR(_log) << "DelegateIdentityManager::ReadAddressAd prequel read error: " << ec.message();
            cb(nullptr);
            return;
        }

        bool error = false;
        logos::bufferstream stream(buf->data(), buf->size());
        auto prequel = std::make_shared<PrequelAddressAd>(error, stream);
        if (error)
        {
            LOG_ERROR(_log) << "DelegateIdentityManager::ReadAddressAd prequel deserialization error";
            cb(nullptr);
            return;
        }

        // check for bogus data
        if (prequel->delegate_id > NUM_DELEGATES - 1 ||
                prequel->epoch_number > ConsensusContainer::GetCurEpochNumber() + INVALID_EPOCH_GAP )
        {
            LOG_ERROR(_log) << "DelegateIdentityManager::ReadAddressAd - Likely received bogus data from unexpected connection."
                            << " epoch number " << (int)prequel->epoch_number
                            << " delegate id " << (int)prequel->delegate_id
                            << " encr delegate id " << (int)prequel->encr_delegate_id
                            << " payload size " << prequel->payload_size;
            cb(nullptr);
            return;
        }

        // Read the rest of the ad
        auto buf_ad = std::make_shared<std::vector<uint8_t>>(prequel->payload_size);
        boost::asio::async_read(*socket,
                                boost::asio::buffer(buf_ad->data(), buf_ad->size()),
                                [this, socket, prequel, buf_ad, cb](const ErrorCode &ec, size_t size) {
            if (ec)
            {
                LOG_ERROR(_log) << "DelegateIdentityManager::ReadAddressAd ad read error: " << ec.message();
                cb(nullptr);
                return;
            }

            try
            {
                bool error = false;
                logos::bufferstream stream(buf_ad->data(), buf_ad->size());
                auto ad = std::make_shared<AddressAd>(error, *prequel, stream, &DelegateIdentityManager::Decrypt);
                if (error)
                {
                    LOG_ERROR(_log) << "DelegateIdentityManager::ReadAddressAd failed to deserialize ad";
                    cb(nullptr);
                    return;
                }
                if (!ValidateSignature(prequel->epoch_number, *ad))
                {
                    LOG_ERROR(_log) << "DelegateIdentityManager::ReadAddressAd, failed to validate AddressAd signature";
                    cb(nullptr);
                    return;
                }

                cb(ad);
            }
            catch (...)
            {
                cb(nullptr);
                LOG_ERROR(_log) << "DelegateIdentityManager::ReadAddressAd, failed to decrypt handshake message";
            }
        });
    });
}

void
DelegateIdentityManager::LoadDB()
{
    logos::transaction transaction (_store.environment, nullptr, true);

    logos::block_store::ad_key adKey;
    logos::block_store::ad_txa_key adTxaKey;
    auto current_epoch_number = ConsensusContainer::GetCurEpochNumber();
    std::vector<logos::block_store::ad_key> ad2del;
    std::vector<logos::block_store::ad_txa_key> adtxa2del;

    for (auto it = logos::store_iterator(transaction, _store.address_ad_db);
         it != logos::store_iterator(nullptr);
         ++it)
    {
        bool error (false);
        assert(sizeof(adKey) == it->first.size());
        memcpy(&adKey, it->first.data(), it->first.size());
        if (adKey.epoch_number < current_epoch_number)
        {
            ad2del.push_back(adKey);
            continue;
        }

        /// all ad messages are saved to the database even if they are encrypted
        /// with another delegate id so that the delegate can respond to peer request
        /// for ad messages. we only store in memory messages encryped with this delegate id
        auto idx = GetDelegateIdFromCache(adKey.epoch_number);
        if (idx == adKey.encr_delegate_id) {
            try {
                logos::bufferstream stream(reinterpret_cast<uint8_t const *> (it->second.data()), it->second.size());
                AddressAd ad(error, stream, &DelegateIdentityManager::Decrypt);
                assert (!error);
                {
                    std::lock_guard<std::mutex> lock(_ad_mutex);
                    std::string ip = ad.GetIP();
                    _address_ad.emplace(std::piecewise_construct,
                                        std::forward_as_tuple(ad.epoch_number, ad.delegate_id),
                                        std::forward_as_tuple(ip, ad.port));
                    LOG_DEBUG(_log) << "DelegateIdentityManager::LoadDB, ad epoch_number " << ad.epoch_number
                                    << " delegate id " << (int) ad.delegate_id
                                    << " ip " << ip << " port " << ad.port;
                }
            }
            catch (const std::exception &e) {
                LOG_ERROR(_log) << "DelegateIdentityManager::LoadDB, failed: " << e.what();
            }
        }
    }

    for (auto it : ad2del)
    {
        _store.ad_del<logos::block_store::ad_key>(transaction, it.epoch_number, it.delegate_id, it.encr_delegate_id);
    }


    for (auto it = logos::store_iterator(transaction, _store.address_ad_txa_db);
         it != logos::store_iterator(nullptr);
         ++it)
    {
        bool error (false);
        assert(sizeof(adTxaKey) == it->first.size());
        memcpy(&adTxaKey, it->first.data(), it->first.size());
        if (adTxaKey.epoch_number < current_epoch_number)
        {
            adtxa2del.push_back(adTxaKey);
            continue;
        }

        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (it->second.data ()), it->second.size ());
        AddressAdTxAcceptor ad (error, stream);
        std::string ip = ad.GetIP();
        assert (!error);
        {
            std::lock_guard<std::mutex> lock(_ad_mutex);
            _address_ad_txa.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(ad.epoch_number, ad.delegate_id),
                                    std::forward_as_tuple(ip, ad.port, ad.json_port));
            LOG_DEBUG(_log) << "DelegateIdentityManager::LoadDB, ad txa epoch_number " << ad.epoch_number
                            << " delegate id " << (int)ad.delegate_id
                            << " ip " << ip << " port " << ad.port << " json port " << ad.json_port;
        }
    }

    for (auto it : adtxa2del)
    {
        _store.ad_del<logos::block_store::ad_txa_key>(transaction, it.epoch_number, it.delegate_id);
    }
}

void
DelegateIdentityManager::TxAcceptorHandshake(std::shared_ptr<Socket> socket,
                                             uint32_t epoch_number,
                                             uint8_t delegate_id,
                                             const char *ip,
                                             uint16_t port,
                                             uint16_t json_port,
                                             std::function<void(bool result)> cb)
{
    auto buf = MakeSerializedAd<AddressAdTxAcceptor>([](auto ad, logos::vectorstream &s)->size_t{
        return (*ad).Serialize(s);
    }, false, epoch_number, delegate_id, ip, port, json_port);

    boost::asio::async_write(*socket,
                             boost::asio::buffer(buf->data(), buf->size()),
                             [this, socket, buf, cb](const ErrorCode &ec, size_t size) {
        if (ec)
        {
            LOG_ERROR(_log) << "DelegateIdentityManager::TxAcceptorHandshake write error " << ec.message();
            cb(false);
        }
        else
        {
            LOG_DEBUG(_log) << "DelegateIdentityManager::TxAcceptorHandshake wrote ad, size " << buf->size();
            cb(true);
        }
    });
}

void
DelegateIdentityManager::ValidateTxAcceptorConnection(std::shared_ptr<Socket> socket,
                                                      const bls::PublicKey &bls_pub,
                                                      std::function<void(bool result, const char *error)> cb)
{
    auto buf = std::make_shared<std::vector<uint8_t>>(PrequelAddressAd::SIZE);

    boost::asio::async_read(*socket,
                            boost::asio::buffer(buf->data(), buf->size()),
                            [socket, buf, bls_pub, cb](const ErrorCode &ec, size_t size){
        if (ec)
        {
            cb(false, "DelegateIdentityManager::ValidateTxAcceptorConnection failed to read tx acceptor prequel");
            return;
        }

        bool error = false;
        PrequelAddressAd prequel(error, *buf);
        if (error)
        {
            cb(false, "DelegateIdentityManager::ValidateTxAcceptorConnection failed to deserialize prequel");
            return;
        }

        auto buf_ad = std::make_shared<std::vector<uint8_t>>(prequel.payload_size);

        boost::asio::async_read(*socket,
                                boost::asio::buffer(buf_ad->data(), buf_ad->size()),
                                [socket, buf_ad, prequel, bls_pub, cb](const ErrorCode &ec, size_t size){
            if (ec)
            {
                cb(false, "DelegateIdentityManager::ValidateTxAcceptorConnection failed to read tx acceptor ad");
                return;
            }

            bool error = false;
            logos::bufferstream stream(buf_ad->data(), buf_ad->size());
            AddressAdTxAcceptor ad(error, prequel, stream);
            if (error)
            {
                cb(false, "DelegateIdentityManager::ValidateTxAcceptorConnection failed to deserialize ad");
                return;
            }
            if (!MessageValidator::Validate(ad.Hash(), ad.signature, bls_pub))
            {
                cb(false, "DelegateIdentityManager::ValidateTxAcceptorConnection failed to validate ad signature");
                return;
            }

            cb(true, "");
        });
    });
}

void
DelegateIdentityManager::ScheduleAd()
{
    auto tomsec = [](auto m) { return boost::posix_time::milliseconds(TConvert<Milliseconds>(m).count()); };
    EpochTimeUtil util;

    auto lapse = util.GetNextEpochTime(_store.is_first_epoch() || _node._recall_handler.IsRecall());

    auto r1 = GetRandAdTime(AD_TIMEOUT_1);
    auto r2 = GetRandAdTime(AD_TIMEOUT_2);
    auto msec = tomsec(lapse + EPOCH_PROPOSAL_TIME - r1);
    if (lapse > (AD_TIMEOUT_1+TIMEOUT_SPREAD))
    {
        msec = tomsec(lapse - r1);
    }
    else if (lapse > (AD_TIMEOUT_2+TIMEOUT_SPREAD))
    {
        msec = tomsec(lapse - r2);
    }

    auto t = boost::posix_time::microsec_clock::local_time() + msec;
    LOG_DEBUG(_log) << "DelegateIdentityManager::ScheduleAd scheduling at "
                    << boost::posix_time::to_simple_string(t) << " lapse " << msec;

    ScheduleAd(msec);
}

void
DelegateIdentityManager::ScheduleAd(boost::posix_time::milliseconds msec)
{
    _timer.expires_from_now(msec);
    _timer.async_wait(std::bind(&DelegateIdentityManager::Advert, this, std::placeholders::_1));
}

void
DelegateIdentityManager::Advert(const ErrorCode &ec)
{
    if (ec)
    {
        LOG_DEBUG(_log) << "DelegateIdentityManager::Advert, error " << ec.message();
        return;
    }

    ApprovedEB eb;
    GetCurrentEpoch(_store, eb);
    CheckAdvertise(CurFromDelegatesEpoch(eb.epoch_number), false);
}

bool
DelegateIdentityManager::OnTxAcceptorUpdate(EpochDelegates epoch,
                                            std::string &ip,
                                            uint16_t port,
                                            uint16_t bin_port,
                                            uint16_t json_port,
                                            bool add)
{
    uint8_t idx = NON_DELEGATE;
    ApprovedEBPtr eb;

    IdentifyDelegates(epoch, idx, eb);
    if (idx == NON_DELEGATE)
    {
        return false;
    }

    auto  current_epoch_number = CurFromDelegatesEpoch(eb->epoch_number);
    if ((add && _address_ad_txa.find({current_epoch_number, idx}) != _address_ad_txa.end()) ||
            (!add && _address_ad_txa.find({current_epoch_number, idx}) == _address_ad_txa.end()))
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(_ad_mutex);
        if (add)
        {
            _address_ad_txa.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(current_epoch_number, idx),
                                    std::forward_as_tuple(ip, bin_port, json_port));
        }
        else
        {
            _address_ad_txa.erase({current_epoch_number, idx});
        }
    }

    AddressAdTxAcceptor ad(current_epoch_number, idx, ip.c_str(), bin_port, json_port, add);
    Sign(current_epoch_number, ad);
    auto buf = std::make_shared<std::vector<uint8_t>>();
    ad.Serialize(*buf);

    UpdateTxAcceptorAdDB(ad, buf->data(), buf->size());

    P2pPropagate(current_epoch_number, idx, buf);

    return _node.update_tx_acceptor(ip, port, add);
}

void
DelegateIdentityManager::UpdateAddressAd(const AddressAd &ad)
{
    std::lock_guard<std::mutex> lock(_ad_mutex);
    std::string ip = ad.GetIP();
    _address_ad.emplace(std::piecewise_construct,
                        std::forward_as_tuple(ad.epoch_number, ad.delegate_id),
                        std::forward_as_tuple(ip, ad.port));
    std::vector<uint8_t> buf;
    ad.Serialize(buf, _ecies_key.pub);
    UpdateAddressAdDB(ad, buf.data(), buf.size());
}

void
DelegateIdentityManager::UpdateAddressAd(uint32_t epoch_number, uint8_t delegate_id)
{
    auto &config = _node.config.consensus_manager_config;
    AddressAd ad(epoch_number, delegate_id, delegate_id, config.local_address.c_str(), config.peer_port);
    UpdateAddressAd(ad);
}

uint8_t
DelegateIdentityManager::GetDelegateIdFromCache(uint32_t cur_epoch_number) {
    std::lock_guard<std::mutex> lock(_cache_mutex);
    auto it = _idx_cache.find(cur_epoch_number);
    if (it != _idx_cache.end()) {
        return it->second;
    } else {
        uint8_t idx;
        IdentifyDelegates(CurToDelegatesEpoch(cur_epoch_number), idx);
        _idx_cache.emplace(cur_epoch_number, idx);
        if (_idx_cache.size() > MAX_CACHE_SIZE) {
            _idx_cache.erase(_idx_cache.begin());
        }
        return idx;
    }
}
