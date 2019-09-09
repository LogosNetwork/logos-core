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

#include <stdlib.h>
#include <unistd.h>

#include <thread>

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
/// ONLY FOR GENERATING LOGS (REMOVE LATER)
void
DelegateIdentityManager::CreateGenesisBlocks(logos::transaction &transaction, logos::genesis_config &config)
{
    LOG_INFO(_log) << "DelegateIdentityManager::CreateGenesisBlocks, creating genesis blocks";

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
        micro_block.sequence = e;
        micro_block.timestamp = 0;
        micro_block.previous = microblock_hash;
        micro_block.last_micro_block = 1;
        microblock_hash = micro_block.Hash();
        auto microblock_tip = micro_block.CreateTip();
        LOG_INFO(_log) << "genmicro {\"epoch_number\": \"" << std::to_string(e) << "\", \"previous\": \"" <<
            micro_block.previous.to_string() << "\", \"hash\": \"" << microblock_hash.to_string() << "\"}";
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
            //char buff[5];
            //sprintf(buff, "%02x", del + 1);
            //logos::keypair pair(buff);
            logos::public_key pub = config.accounts[(int)i];
            //Amount stake = 100000 + (uint64_t)del * 100;
            Amount stake = config.amount[(int)i];
            //Delegate delegate = {pub, dpk, ecies_key, 100000 + (uint64_t)del * 100, stake};
            Delegate delegate = {pub, dpk, ecies_key, stake, stake};
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
                start.origin = pub;
                start.stake = stake;
                start.Sign(config.priv[i], pub);
                rep.rep_action_tip = start.Hash();
                LOG_INFO(_log) << "genstartrep {\"origin\": \"" << pub.to_string() << "\", \"stake\": \"" <<
                    stake.to_string_dec() << "\", \"signature\": \"" << start.signature.to_string() << "\"}";

                if (_store.request_put(start, transaction))
                {
                    LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update StartRepresenting";
                    trace_and_halt();
                }
                //dummy request for epoch transition
                AnnounceCandidacy announce;
                announce.epoch_num = 0;
                announce.origin = pub;
                announce.stake = stake;
                announce.bls_key = dpk;
                announce.ecies_key = ecies_key;
                announce.Sign(config.priv[i], pub);

                LOG_INFO(_log) << "genannounce {\"origin\": \"" << pub.to_string() << "\", \"stake\": \"" <<
                    stake.to_string_dec() << "\", \"bls\": \"" << dpk.to_string() << "\", \"ecies\": \"" <<
                    ecies_key.ToHexString() << "\", \"signature\": \"" << announce.signature.to_string() << "\"}";


                rep.candidacy_action_tip = announce.Hash();
                if (_store.request_put(announce, transaction) || _store.rep_put(pub, rep, transaction))
                {
                    LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update AnnounceCandidacy";
                    trace_and_halt();
                }
                VotingPowerManager::GetInstance()->AddSelfStake(pub,stake,0,transaction);
                CandidateInfo candidate;
                candidate.next_stake = stake;
                candidate.cur_stake = stake;
                candidate.bls_key = dpk;
                candidate.ecies_key = ecies_key;
                //TODO: should we put these accounts into candidate list even
                //though elections are not being held yet?
                if (_store.candidate_put(pub, candidate, transaction))
                {
                    LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update CandidateInfo";
                    trace_and_halt();
                }
            }

            LOG_INFO(_log) << __func__ << "bls public key for delegate i=" << (int)i
                            << " pub_key=" << pub.to_account();
            if(i < NUM_DELEGATES)
            {
                delegate.starting_term = false;
                epoch.delegates[i] = delegate;
            }
        }

        epoch_hash = epoch.Hash();

        LOG_INFO(_log) << "genepoch {\"epoch_number\": \"" << std::to_string(e) <<"\", \"previous\": \"" <<
            epoch.previous.to_string() << "\", \"hash\": \"" << epoch_hash.to_string() << "\", \"delegates\": [";

        for(int i = 0; i < NUM_DELEGATES; i++) {
            LOG_INFO(_log) << "{\"account\": \"" << epoch.delegates[i].account.to_string() << "\", \"bls_pub\": \"" <<
                epoch.delegates[i].bls_pub.to_string() << "\", \"ecies\": \"" << epoch.delegates[i].ecies_pub.ToHexString() <<
                "\", \"vote\": \"" << epoch.delegates[i].vote.to_string_dec() << "\", \"stake\": \"" <<
                epoch.delegates[i].stake.to_string_dec() << "\"}";
        }
        LOG_INFO(_log) << "]}";


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
DelegateIdentityManager::CreateGenesisBlocks(logos::transaction &transaction, GenesisBlock &config)
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
        // Create microblock and place in DB
        microblock_hash = config.gen_micro[e].Hash();
        auto microblock_tip = config.gen_micro[e].CreateTip();

        if (_store.micro_block_put(config.gen_micro[e], transaction) ||
                _store.micro_block_tip_put(microblock_tip, transaction) )
        {
            LOG_FATAL(_log) << "update failed to insert micro_block or micro_block tip"
                            << microblock_hash.to_string();
            trace_and_halt();
            return;
        }
        update("micro block", config.gen_micro[e], microblock_hash,
               &BlockStore::micro_block_get, &BlockStore::micro_block_put);

        // Create epochs and place in DB
        config.gen_epoch[e].micro_block_tip = microblock_tip;
        epoch_hash = config.gen_epoch[e].Hash();
        if(_store.epoch_put(config.gen_epoch[e], transaction) ||
                _store.epoch_tip_put(config.gen_epoch[e].CreateTip(), transaction))
        {
            LOG_FATAL(_log) << "update failed to insert epoch or epoch tip"
                            << epoch_hash.to_string();
            trace_and_halt();
            return;
        }
        update("epoch", config.gen_epoch[e], epoch_hash,
               &BlockStore::epoch_get, &BlockStore::epoch_put);
    }

    for (int del = 0; del < NUM_DELEGATES*2; ++del)
    {
        // TODO: REPLACE ONCE IM IS IN
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        stringstream str(bls_keys[del]);
        bls::KeyPair bls_key;
        str >> bls_key.prv >> bls_key.pub;
        ECIESKeyPair ecies_key(ecies_keys[del]);

        // StartRepresenting requests
        RepInfo rep;
        rep.rep_action_tip = config.start[del].Hash();
        if (_store.request_put(config.start[del], transaction))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update StartRepresenting";
                    trace_and_halt();
        }

        // AnnounceCandidacy requests
        rep.candidacy_action_tip = config.announce[del].Hash();
        if (_store.request_put(config.announce[del], transaction) || _store.rep_put(config.announce[del].origin, rep, transaction))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update AnnounceCandidacy";
            trace_and_halt();
        }
        VotingPowerManager::GetInstance()->AddSelfStake(config.announce[del].origin,config.announce[del].stake,0,transaction);

        // CandidateInfo
        if (_store.candidate_put(config.announce[del].origin, config.candidate[del], transaction))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisBlocks, failed to update CandidateInfo";
            trace_and_halt();
        }

        // TODO: REMOVE ONCE IM IS IN
        logos::genesis_delegate delegate{config.start[del].origin, bls_key, ecies_key, config.start[del].stake, config.start[del].stake};
        logos::genesis_delegates.push_back(delegate);
    }
}

void
DelegateIdentityManager::Init(const Config &config)
{
    uint32_t epoch_number = 0;
    {
        logos::transaction transaction(_store.environment, nullptr, true);

        const ConsensusManagerConfig &cmconfig = _node.config.consensus_manager_config;
        _epoch_transition_enabled = cmconfig.enable_epoch_transition;

        EpochVotingManager::ENABLE_ELECTIONS = cmconfig.enable_elections;

        /*
        boost::filesystem::path data_path(boost::filesystem::current_path());
        logos::genesis_config genconfig;
        auto config_path((data_path / "genesis.json"));
        std::fstream config_file;
        auto error(logos::fetch_object(genconfig, config_path, config_file));
        */

        NTPClient nt("pool.ntp.org");

        nt.asyncNTP();
        if (nt.computeDelta() > 20)
        {
            LOG_INFO(_log) << "NTP is too much out of sync";
            trace_and_halt();
        }

        boost::filesystem::path gen_data_path(boost::filesystem::current_path());
        GenesisBlock genesisBlock;
        std::fstream gen_config_file;



        Tip epoch_tip;
        BlockHash &epoch_tip_hash = epoch_tip.digest;
        if (_store.epoch_tip_get(epoch_tip)) {
            auto gen_config_path((gen_data_path / "genlogos.json"));
            auto error(logos::fetch_object(genesisBlock, gen_config_path, gen_config_file));

            if(!genesisBlock.VerifySignature(logos::test_genesis_key.pub))
            {
                LOG_INFO(_log) << "Genesis Log input failed signature " << genesisBlock.signature.to_string();
                trace_and_halt();
            }

            //CreateGenesisBlocks(transaction, genconfig);
            CreateGenesisBlocks(transaction, genesisBlock);
            epoch_number = GENESIS_EPOCH + 1;
        } else {
            ApprovedEB previous_epoch;
            if (_store.epoch_get(epoch_tip_hash, previous_epoch)) {
                LOG_FATAL(_log) << "DelegateIdentityManager::Init Failed to get epoch: " << epoch_tip.to_string();
                trace_and_halt();
            }

            LOG_INFO(_log) << "DelegateIdentityManager::Init to get epoch: " << epoch_tip.to_string();
            // if a node starts after epoch transition start but before the last microblock
            // is proposed then the latest epoch block is not created yet and the epoch number
            // has to be incremented by 1
            epoch_number = previous_epoch.epoch_number + 1;
            epoch_number = (StaleEpoch()) ? epoch_number + 1 : epoch_number;
        }

        // check account_db
        if (_store.account_db_empty()) {
            auto error(false);

            // Construct genesis open block
            boost::property_tree::ptree tree;
            std::stringstream istream(logos::logos_test_genesis);
            boost::property_tree::read_json(istream, tree);
            Send logos_genesis_block(error, tree);
            if (error) {
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
                                   transaction)) {
                LOG_FATAL(_log) << "DelegateIdentityManager::Init failed to update the database";
                trace_and_halt();
            }
            CreateGenesisAccounts(transaction, genesisBlock);
            //CreateGenesisAccounts(transaction, genconfig, genesisBlock);
        } else { LoadGenesisAccounts(genesisBlock); }

        // TODO, wallet integration
        _delegate_account = logos::genesis_delegates[cmconfig.delegate_id].key;
        _ecies_key = logos::genesis_delegates[cmconfig.delegate_id].ecies_key;
        *_bls_key = logos::genesis_delegates[cmconfig.delegate_id].bls_key;
        _global_delegate_idx = cmconfig.delegate_id;
        LOG_INFO(_log) << "delegate id is " << (int) _global_delegate_idx;

        ConsensusContainer::SetCurEpochNumber(epoch_number);
    }
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE PRIVATE KEYS ARE 0-63!!! TBD
/// THE SAME GOES FOR BLS KEYS
/// ONLY FOR GENERATING LOG
void
DelegateIdentityManager::CreateGenesisAccounts(logos::transaction &transaction, logos::genesis_config &config, GenesisBlock &config1)
{

    LOG_INFO(_log) << "DelegateIdentityManager::CreateGenesisBlocks, creating genesis accounts";
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

        logos::public_key pub = config.accounts[del];

        logos::genesis_delegate delegate{pub, bls_key, ecies_key,
                                         100000 + (uint64_t) del * 100, 100000 + (uint64_t) del * 100};
        //logos::keypair &pair = delegate.key;
        //pub = pair.pub;

        logos::genesis_delegates.push_back(delegate);

        //logos::amount amount((del + 1) * 1000000 * PersistenceManager<R>::MIN_TRANSACTION_FEE);
        logos::amount amount(config.amount[del]);

        Send request(logos::logos_test_account,   // account
                     genesis_account.head,        // previous
                     genesis_account.block_count, // sequence
                     pub,                         // link/to
                     amount,
                     0,                           // transaction fee
                     logos::test_genesis_key.prv.data, // SG: Sign with correct key
                     logos::test_genesis_key.pub);

        LOG_INFO(_log) << "genaccount {\"account\": \"" << pub.to_string() << "\", \"amount\": \""<<
            amount.to_string_dec() << "\", \"previous\": \"" << genesis_account.head.to_string() << "\", \"sequence\": \"" <<
            std::to_string(genesis_account.block_count) << "\", \"signature\": \"" << request.signature.to_string() << "\"}";
        LOG_INFO(_log) << "sendhash " << request.GetHash().to_string();
        genesis_account.SetBalance(genesis_account.GetBalance() - amount,0,transaction);
        genesis_account.head = request.GetHash();
        genesis_account.block_count++;
        genesis_account.modified = logos::seconds_since_epoch();

        ReceiveBlock receive(0, request.GetHash(), 0);

        if (_store.request_put(request, transaction) ||
            _store.receive_put(receive.Hash(), receive, transaction) ||
            _store.account_put(pub,
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
DelegateIdentityManager::CreateGenesisAccounts(logos::transaction &transaction, GenesisBlock &config)
{
    LOG_INFO(_log) << "DelegateIdentityManager::CreateGenesisBlocks, creating genesis accounts";
    logos::account_info genesis_account;
    if (_store.account_get(logos::logos_test_account, genesis_account, transaction))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::CreateGenesisAccounts, failed to get account";
        trace_and_halt();
    }

    // create genesis delegate accounts
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {

        genesis_account.SetBalance(genesis_account.GetBalance() - config.gen_sends[del].transactions.front().amount,0,transaction);
        genesis_account.head = config.gen_sends[del].GetHash();
        genesis_account.block_count++;
        genesis_account.modified = logos::seconds_since_epoch();

        ReceiveBlock receive(0, config.gen_sends[del].GetHash(), 0);

        if (_store.request_put(config.gen_sends[del], transaction) ||
            _store.receive_put(receive.Hash(), receive, transaction) ||
            _store.account_put(config.gen_sends[del].transactions.front().destination,
                           {
                               /* Head          */ 0,
                               /* Receive       */ receive.Hash(),
                               /* Rep           */ 0,
                               /* Open          */ config.gen_sends[del].GetHash(),
                               /* Amount        */ config.gen_sends[del].transactions.front().amount,
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
DelegateIdentityManager::LoadGenesisAccounts(logos::genesis_config &config)
{
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        stringstream str(bls_keys[del]);
        bls::KeyPair bls_key;
        str >> bls_key.prv >> bls_key.pub;
        ECIESKeyPair ecies_key(ecies_keys[del]);
        logos::public_key pub = config.accounts[del];
        logos::genesis_delegate delegate{pub, bls_key, ecies_key,
                                         100000 + (uint64_t) del * 100, 100000 + (uint64_t) del * 100};
        //logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);
    }
}

void
DelegateIdentityManager::LoadGenesisAccounts(GenesisBlock &config)
{
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        stringstream str(bls_keys[del]);
        bls::KeyPair bls_key;
        str >> bls_key.prv >> bls_key.pub;
        ECIESKeyPair ecies_key(ecies_keys[del]);
        logos::public_key pub = config.gen_sends[del].transactions.front().destination;
        logos::genesis_delegate delegate{pub, bls_key, ecies_key,
                                         config.gen_epoch[0].delegates[del].vote, config.gen_epoch[0].delegates[del].stake};
        //logos::keypair &pair = delegate.key;

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

    if (current_epoch_number <= GENESIS_EPOCH+1)
    {

        //_node.p2p.get_peers(this, 4);

        ApprovedEBPtr eb;
        IdentifyDelegates(2, idx, eb);

        if (idx != NON_DELEGATE) {
            //std::cout << std::to_string(idx) << std::endl;
            auto ids = GetDelegatesToAdvertise(idx);
            Advertise(2, idx, eb, ids);
            UpdateAddressAd(2, idx);
        }
        LoadDB();
        //auto now = boost::posix_time::milliseconds(0);
        //ScheduleAd(now);
    }

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
    DelegateIdentityManager::Sign(ad.Hash(), ad.signature);
//    auto validator = _validator_builder.GetValidator(epoch_number);
//    if(validator == nullptr)
//    	return;
//    auto hash = ad.Hash();
//    validator->Sign(hash, ad.signature);
}

bool
DelegateIdentityManager::ValidateSignature(uint32_t epoch_number, const CommonAddressAd &ad)
{
    auto hash = ad.Hash();
    auto validator = _validator_builder.GetValidator(epoch_number);
    if(validator == nullptr)
    	return false;
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
DelegateIdentityManager::ServerHandshake(
        std::shared_ptr<Socket> socket,
        PeerBinder & binder,
        std::function<void(std::shared_ptr<AddressAd>)> cb)
{
    ReadAddressAd(socket, [this, socket, cb, &binder](std::shared_ptr<AddressAd> ad){
        if (!ad)
        {
            LOG_DEBUG(_log) << "DelegateIdentityManager::ServerHandshake failed to read client's ad";
            cb(nullptr);
            return;
        }
        if(!binder.CanBind(ad->epoch_number))
        {
            LOG_ERROR(_log) << "DelegateIdentityManager::ServerHandshake - "
                << "cannot bind for epoch_number=" << ad->epoch_number;
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
                             [this, socket, buf, cb, remote_delegate_id, epoch_number](const ErrorCode &ec, size_t size){
        if (ec)
        {
            LOG_ERROR(_log) << "DelegateIdentityManager::WriteAddressAd write error " << ec.message()
            << ",remote_delegate_id=" << (int) remote_delegate_id
            << ",epoch_number=" << epoch_number;
            cb(false);
        }
        else
        {
            LOG_DEBUG(_log) << "DelegateIdentityManager::WriteAddressAd wrote ad, size " << buf->size()
            << ",remote_delegate_id=" << (int) remote_delegate_id
            << ",epoch_number=" << epoch_number;
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
        else
        {
            LOG_INFO(_log) << "DelegateIdentityManager::ReadAddressAd successful";
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
        /// for ad messages. we only store in memory messages encrypted with this delegate id
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
    //TODO Peng: at least 5 minutes, revisit after IM merge
    msec = std::max(msec, boost::posix_time::milliseconds(1000*60*5));

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

logos::genesis_config::genesis_config ()
{}

bool logos::genesis_config::deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
{
    auto error (false);
    try
    {
        std::stringstream ss;
        boost::property_tree::json_parser::write_json(ss, tree_a);

        //std::cout << ss.str() << std::endl;
        auto accnts (tree_a.get_child("accounts"));
        for (int del = 0; del < NUM_DELEGATES*2; ++del) {
            auto idx (accnts.get_child(std::to_string(del)));
            //accounts[del] = idx.get<std::string>("account");
            auto error = accounts[del].decode_account(idx.get<std::string>("account"));
            priv[del].decode_hex(idx.get<std::string>("private"));
            amount[del].decode_dec(idx.get<std::string>("amount"));
            //LOG_DEBUG(_log) << "SGU AMOUNT:  " << idx.get<std::string>("amount");
        }
        //auto idx (accounts.get_child("0"));
        //auto account (idx.get<std::string>("account"));
        //std::cout << account << std::endl;
        //LOG_DEBUG(_log) << "SGU ACCOUNT: " << account;
    }
    catch (std::runtime_error const &)
    {
        return false;
    }

    return true;
}


GenesisBlock::GenesisBlock()
{}

bool GenesisBlock::deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
{
    digest = BlockHash(0);
    blake2b_state hash;

    auto status(blake2b_init(&hash, sizeof(BlockHash)));
    assert(status == 0);
    auto error (false);
    try
    {
        std::stringstream ss;
        boost::property_tree::json_parser::write_json(ss, tree_a);

        auto accnts (tree_a.get_child("accounts"));
        int idx = 0;
        boost::property_tree::ptree::const_iterator end = accnts.end();
        for (boost::property_tree::ptree::const_iterator it = accnts.begin(); it != end; ++it)
        {
            AccountAddress account;
            account.decode_hex(it->second.get<std::string>("account"));
            //del_accounts[idx].decode_hex(it->second.get<std::string>("account"));
            Amount amount;
            amount.decode_dec(it->second.get<std::string>("amount"));
            BlockHash previous;
            previous.decode_hex(it->second.get<std::string>("previous"));
            //del_amount[idx].decode_dec(it->second.get<std::string>("amount"));
            uint32_t sequence = it->second.get<uint32_t>("sequence");
            AccountSig signature;
            signature.decode_hex(it->second.get<std::string>("signature"));
            uint64_t work = 0;

            gen_sends[idx] = Send(logos::logos_test_account,   // account
                     previous,                                 // previous
                     sequence,                                 // sequence
                     account,                                  // link/to
                     amount,
                     0,                                        // transaction fee
                     signature,
                     work);

            gen_sends[idx].Hash(hash);
            idx++;
        }

        // Deserialize microblocks from config
        auto micro (tree_a.get_child("micros"));
        idx = 0;
        end = micro.end();
        for (boost::property_tree::ptree::const_iterator it = micro.begin(); it != end; ++it)
        {
            gen_micro[idx].primary_delegate = 0xff;
            gen_micro[idx].epoch_number = it->second.get<uint32_t>("epoch_number");
            gen_micro[idx].sequence = 0;
            gen_micro[idx].timestamp = 0;
            gen_micro[idx].previous.decode_hex(it->second.get<std::string>("previous"));
            gen_micro[idx].last_micro_block = 0;
            gen_micro[idx].Hash(hash);
            idx++;
        }

        // Deserialize epochs from config
        auto epoch (tree_a.get_child("epochs"));
        idx = 0;
        end = epoch.end();
        for (boost::property_tree::ptree::const_iterator it = epoch.begin(); it != end; ++it)
        {
            gen_epoch[idx].primary_delegate = 0xff;
            gen_epoch[idx].epoch_number = it->second.get<uint32_t>("epoch_number");
            gen_epoch[idx].sequence = 0;
            gen_epoch[idx].timestamp = 0;
            gen_epoch[idx].total_RBs = 0;
            gen_epoch[idx].previous.decode_hex(it->second.get<std::string>("previous"));
            auto delegates (it->second.get_child("delegates"));
            int del_idx = 0;
            boost::property_tree::ptree::const_iterator end1 = delegates.end();
            for (boost::property_tree::ptree::const_iterator it = delegates.begin(); it != end1; ++it)
            {
                logos::public_key pub;
                pub.decode_hex(it->second.get<std::string>("account"));
                DelegatePubKey dpk(it->second.get<std::string>("bls_pub"));
                ECIESPublicKey ecies_key;
                ecies_key.FromHexString(it->second.get<std::string>("ecies"));
                Amount stake, vote;
                stake.decode_dec(it->second.get<std::string>("stake"));
                vote.decode_dec(it->second.get<std::string>("vote"));
                Delegate delegate = {pub, dpk, ecies_key, stake, stake};
                delegate.starting_term = false;
                gen_epoch[idx].delegates[del_idx] = delegate;
                del_idx++;
            }
            gen_epoch[idx].Hash(hash);
            idx++;
        }

        // Deserialize StartRepresenting requests for genesis delegates from config
        auto starts (tree_a.get_child("start"));
        idx = 0;
        end = starts.end();
        for (boost::property_tree::ptree::const_iterator it = starts.begin(); it != end; ++it)
        {
            logos::public_key pub;
            pub.decode_hex(it->second.get<std::string>("origin"));
            Amount stake;
            stake.decode_dec(it->second.get<std::string>("stake"));
            start[idx].epoch_num = 0;
            start[idx].origin = pub;
            start[idx].stake = stake;
            start[idx].set_stake = true;
            start[idx].signature.decode_hex(it->second.get<std::string>("signature"));
            start[idx].Hash(hash);
            idx++;
        }

        // Deserialize AnnounceCandidacy requests for genesis delegates from config
        auto announces (tree_a.get_child("announce"));
        idx = 0;
        end = announces.end();
        for (boost::property_tree::ptree::const_iterator it = announces.begin(); it != end; ++it)
        {
            logos::public_key pub;
            pub.decode_hex(it->second.get<std::string>("origin"));
            Amount stake;
            stake.decode_dec(it->second.get<std::string>("stake"));
            announce[idx].epoch_num = 0;
            announce[idx].origin = pub;
            announce[idx].stake = stake;
            announce[idx].ecies_key.FromHexString(it->second.get<std::string>("ecies"));
            DelegatePubKey dpk(it->second.get<std::string>("bls"));
            announce[idx].bls_key = dpk;
            announce[idx].signature.decode_hex(it->second.get<std::string>("signature"));

            // Create corresponding CandidateInfo for each genesis delegate
            candidate[idx].next_stake = stake;
            candidate[idx].cur_stake = stake;
            candidate[idx].bls_key = dpk;
            candidate[idx].ecies_key = announce[idx].ecies_key;

            announce[idx].Hash(hash);
            idx++;
        }

        // Get signature from config
        signature.decode_hex(tree_a.get<std::string>("signature"));
        status = blake2b_final(&hash, digest.data(), sizeof(BlockHash));
        //assert(status == 0);
        //LOG_DEBUG(_log) << "SGU DEBUG HASH " << digest.to_string();
        //Sign(logos::test_genesis_key.prv.data, logos::test_genesis_key.pub);
        //LOG_DEBUG(_log) << "SGU DEBUG SIGNATURE " << signature.to_string();
    }
    catch (std::runtime_error const &)
    {
        return false;
    }

    return true;
}

// For creating logs only (remove later)
void logos::genesis_config::Sign(AccountPrivKey const & priv, AccountPubKey const & pub)
{

    ed25519_sign(const_cast<BlockHash&>(digest).data(),
                 HASH_SIZE,
                 const_cast<AccountPrivKey&>(priv).data(),
                 const_cast<AccountPubKey&>(pub).data(),
                 signature.data());
}

bool GenesisBlock::VerifySignature(AccountPubKey const & pub) const
{
    return 0 == ed25519_sign_open(const_cast<BlockHash &>(digest).data(),
                                  HASH_SIZE,
                                  const_cast<AccountPubKey&>(pub).data(),
                                  const_cast<AccountSig&>(signature).data());
}

// For creating logs only (remove later)
void GenesisBlock::Sign(AccountPrivKey const & priv, AccountPubKey const & pub)
{

    ed25519_sign(const_cast<BlockHash&>(digest).data(),
                 HASH_SIZE,
                 const_cast<AccountPrivKey&>(priv).data(),
                 const_cast<AccountPubKey&>(pub).data(),
                 signature.data());
}

bool GenesisBlock::Validate(logos::process_return & result) const
{
    for (int i = 0; i < NUM_DELEGATES*2; ++i)
    {
        if (start[i].stake != gen_epoch[0].delegates[i].stake)
            return false;

        if (announce[i].stake != gen_epoch[0].delegates[i].stake)
            return false;
    }
    return true;
}

/**
 *  NTPClient
 *  @Param i_hostname - The time server host name which you are connecting to obtain the time
 *                      eg. the pool.ntp.org project virtual cluster of timeservers
 */
NTPClient::NTPClient(string i_hostname)
    :_host_name(i_hostname),_port(123),_ntp_time(0),_delay(0)
{
    //Host name is defined by you and port number is 123 for time protocol
}

/**
 * RequestDatetime_UNIX()
 * @Returns long - number of seconds from the Unix Epoch start time
 */
long NTPClient::RequestDatetime_UNIX()
{
    return NTPClient::RequestDatetime_UNIX_s(this);
}

long NTPClient::RequestDatetime_UNIX_s(NTPClient *this_l)
{
    time_t timeRecv;

    boost::asio::io_service io_service;

    boost::asio::ip::udp::resolver resolver(io_service);
    boost::asio::ip::udp::resolver::query query(
                                                 boost::asio::ip::udp::v4(),
                                                 this_l->_host_name,
                                                 "ntp");

    boost::asio::ip::udp::endpoint receiver_endpoint = *resolver.resolve(query);

    boost::asio::ip::udp::socket socket(io_service);
    socket.open(boost::asio::ip::udp::v4());

    boost::array<unsigned char, 48> sendBuf  = {010,0,0,0,0,0,0,0,0};

    socket.send_to(boost::asio::buffer(sendBuf), receiver_endpoint);

    boost::array<unsigned long, 1024> recvBuf;
    boost::asio::ip::udp::endpoint sender_endpoint;

    try{
        size_t len = socket.receive_from(
                                            boost::asio::buffer(recvBuf),
                                            sender_endpoint
                                        );

        timeRecv = ntohl((time_t)recvBuf[4]);

        timeRecv-= 2208988800U;  //Unix time starts from 01/01/1970 == 2208988800U

    }catch (std::exception& e){

        std::cerr << e.what() << std::endl;

    }

    this_l->setNtpTime(timeRecv);
    return timeRecv;
}

void NTPClient::timeout_s(NTPClient *this_l)
{
    int count = 0;
    static const int MAX = NTPClient::MAX_TIMEOUT;

    while(true) {
        if(this_l->getNtpTime()) {
            return;
        }
        if(count++ > MAX) {
            break;
        }
        sleep(1);
    }

    std::cout << "NTPClient::timeout_s udp socket timed out\n";
}

void NTPClient::asyncNTP()
{
    _ntp_time = 0;
    std::thread t1(NTPClient::RequestDatetime_UNIX_s,this);
    t1.detach();
    std::thread t2(NTPClient::timeout_s,this);
    t2.join();
}

bool NTPClient::timedOut()
{
    if(!_ntp_time) {
        return true;
    } else {
        return false;
    }
}

time_t NTPClient::getTime()
{
    return _ntp_time;
}

time_t NTPClient::getDefault()
{
    return (_ntp_time=time(0));
}

void NTPClient::start_s(NTPClient *this_l)
{
    while(true) {
        this_l->asyncNTP();
        if(this_l->timedOut()) {
            this_l->setNtpTime(0);
            this_l->setDelay(0);
        }
        sleep(60*60); // Sleep for one hour.
    }
}

time_t NTPClient::init()
{
    asyncNTP();
    std::thread t1(start_s,this);
    t1.detach();
    return computeDelta();
}

time_t NTPClient::computeDelta()
{
    if(timedOut()) {
        if(_delay) {
            return _delay; // Previous delta.
        } else {
            return (_delay=0); // Zero.
        }
    }
    // compute new delta.
    return (_delay=(abs(time(0) - _ntp_time)));
}

time_t NTPClient::getCurrentDelta()
{
    return _delay;
}

time_t NTPClient::now()
{
    return time(0) + _delay; // Our time + ntp delta.
}