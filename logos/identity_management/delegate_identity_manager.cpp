/// @file
/// This file contains the declaration of the DelegateIdentityManager class, which encapsulates
/// node identity management logic. Currently it holds all delegates ip, accounts, and delegate's index (formerly id)
/// into epoch's voted delegates. It also creates genesis microblocks, epochs, and delegates genesis accounts
///
#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>
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

uint8_t DelegateIdentityManager::_global_delegate_idx = NON_DELEGATE;
bool DelegateIdentityManager::_epoch_transition_enabled = true;
DelegateIdentityManager::ECIESKeyPairPtr DelegateIdentityManager::_ecies_key = nullptr;
DelegateIdentityManager::BLSKeyPairPtr DelegateIdentityManager::_bls_key = nullptr;
constexpr uint8_t DelegateIdentityManager::INVALID_EPOCH_GAP;
constexpr Minutes DelegateIdentityManager::AD_TIMEOUT_1;
constexpr Minutes DelegateIdentityManager::AD_TIMEOUT_2;
constexpr Seconds DelegateIdentityManager::TIMEOUT_SPREAD;

DelegateIdentityManager::DelegateIdentityManager(logos::NodeInterface &node,
                                                 logos::block_store &store,
                                                 boost::asio::io_service &service,
                                                 class Sleeve &sleeve)
    : _store(store)
    , _validator_builder(_store)
    , _timer(service)
    , _node(node)
    , _sleeve(sleeve)
{
    {
        std::lock_guard<std::mutex> lock(_activation_mutex);
        _activated[QueriedEpoch::Current] = false;
        _activated[QueriedEpoch::Next] = false;
    }
    Init();
    LoadDB();
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE HARD-CODED PUB KEYS!!! TODO
void
DelegateIdentityManager::CreateGenesisBlocks(logos::transaction &transaction, GenesisBlock &config)
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

        logos::genesis_delegates.push_back(config.start[del].origin);
    }
}

void
DelegateIdentityManager::Init()
{
    uint32_t epoch_number = 0;
    logos::transaction transaction(_store.environment, nullptr, true);

    const ConsensusManagerConfig &cmconfig = _node.GetConfig().consensus_manager_config;
    _epoch_transition_enabled = cmconfig.enable_epoch_transition;

    EpochVotingManager::ENABLE_ELECTIONS = cmconfig.enable_elections;

    NTPClient nt("pool.ntp.org");  // TODO: remove hard coded value

    int ntp_attempts = 0;
    nt.asyncNTP();
    while (true) {
        if(ntp_attempts >= MAX_NTP_RETRIES) {
            LOG_ERROR(_log) << "DelegateIdentityManage::Init - NTP is too much out of sync";
            trace_and_halt();
        }
        if (nt.computeDelta() < 20) {
            break;
        }
        else {
            nt.asyncNTP();
            ntp_attempts++;
            usleep(1000);
        }
    }

    boost::filesystem::path gen_data_path(_node.GetApplicationPath());
    GenesisBlock genesisBlock;
    std::fstream gen_config_file;

    Tip epoch_tip;
    BlockHash &epoch_tip_hash = epoch_tip.digest;
    if (_store.epoch_tip_get(epoch_tip)) {
        auto gen_config_path((gen_data_path / "genlogos.json"));
        auto status(logos::fetch_object(genesisBlock, gen_config_path, gen_config_file));

        if(!status)
        {
            LOG_ERROR(_log) << "DelegateIdentityManage::Init - failed to read genlogos json file";
            trace_and_halt();
        }

        if(!genesisBlock.VerifySignature(logos::test_genesis_key.pub))
        {
            LOG_ERROR(_log) << "DelegateIdentityManage::Init - genlogos input failed signature " << genesisBlock.signature.to_string();
            trace_and_halt();
        }

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
        // TODO: the StaleEpoch check is inaccurate if we are at GENESIS_EPOCH + 1 due to the extra-long first epoch.
        //  Further logic is needed in the future
        epoch_number = (StaleEpoch()) ? epoch_number + 1 : epoch_number;
    }

    // TODO: handle the edge case where epoch blocks exist but not genesis accounts.

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
    } else { LoadGenesisAccounts(genesisBlock); }

    _global_delegate_idx = cmconfig.delegate_id;

    // Note that epoch number is set again after bootstrapping is complete
    ConsensusContainer::SetCurEpochNumber(epoch_number);

    LOG_DEBUG(_log) << "DelegateIdentityManager::Init - Started identity manager, current epoch number: " << epoch_number;
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE PRIVATE KEYS ARE 0-63!!! TBD
/// THE SAME GOES FOR BLS KEYS
void
DelegateIdentityManager::CreateGenesisAccounts(logos::transaction &transaction, GenesisBlock const &config)
{
    LOG_INFO(_log) << "DelegateIdentityManager::CreateGenesisAccounts, creating genesis accounts";
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
DelegateIdentityManager::LoadGenesisAccounts(GenesisBlock const &config)
{
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        // load EDDSA pub key
        logos::public_key pub = config.gen_sends[del].transactions.front().destination;

        logos::genesis_delegates.push_back(pub);
    }
}

sleeve_status
DelegateIdentityManager::UnlockSleeve(std::string const & password)
{
    logos::transaction tx(_sleeve._env, nullptr, true);
    auto status (_sleeve.Unlock(password, tx));

    if (!status)
        return status;

    // Sleeve is now Unlocked.
    // Check if existing BLS and ECIES keys exist, and enter Sleeved state if so
    LOG_DEBUG(_log) << "DelegateIdentityManager::UnlockSleeve - Sleeve unlocked.";
    std::lock_guard<std::mutex> lock(_activation_mutex);
    if (_sleeve.KeysExist(tx))
    {
        LOG_DEBUG(_log) << "DelegateIdentityManager::UnlockSleeve - Detected governance keys, entering Sleeved state.";
        OnSleeved(tx);
    }

    return status;
}

sleeve_status
DelegateIdentityManager::LockSleeve()
{
    auto status (_sleeve.Lock());

    if (!status)
        return status;

    LOG_DEBUG(_log) << "DelegateIdentityManager::LockSleeve - Sleeve locked, Unsleeving.";
    OnUnsleeved();
    return status;
}

sleeve_status
DelegateIdentityManager::Sleeve(PlainText const & bls_prv, PlainText const & ecies_prv, bool overwrite)
{
    logos::transaction tx(_sleeve._env, nullptr, true);
    auto status (_sleeve.StoreKeys(bls_prv, ecies_prv, overwrite, tx));

    if (!status)
        return status;

    std::lock_guard<std::mutex> lock(_activation_mutex);
    if (status && overwrite && IsSleeved())  // re-entering sleeved state
    {
        LOG_DEBUG(_log) << "DelegateIdentityManager::Sleeve - overwriting existing identity.";
        _node.DeactivateConsensus();
    }

    LOG_DEBUG(_log) << "DelegateIdentityManager::Sleeve - entering Sleeved state.";
    // Entering sleeved state
    OnSleeved(tx);
    return status;
}

sleeve_status
DelegateIdentityManager::Unsleeve()
{
    logos::transaction tx(_sleeve._env, nullptr, true);
    auto status (_sleeve.Unsleeve(tx));

    if (!status)
        return status;

    LOG_DEBUG(_log) << "DelegateIdentityManager::Unsleeve - Unsleeving.";
    OnUnsleeved();
    return status;
}

void
DelegateIdentityManager::ResetSleeve()
{
    logos::transaction tx(_sleeve._env, nullptr, true);
    _sleeve.Reset(tx);
    LOG_DEBUG(_log) << "DelegateIdentityManager::ResetSleeve - Unsleeving.";
    OnUnsleeved();
}

bool
DelegateIdentityManager::IsSettingChangeScheduled()
{
    return _activation_schedule.start_epoch > ConsensusContainer::GetCurEpochNumber();
}

sleeve_status
DelegateIdentityManager::ChangeActivation(bool const & activate, uint32_t const & epoch_num)
{
    std::lock_guard<std::mutex> lock(_activation_mutex);

    // Ignore if we received activate / deactivate when already at the desired setting
    if (activate == _activated[QueriedEpoch::Current])
    {
        sleeve_code ret_code = sleeve_code::setting_already_applied;
        LOG_WARN(_log) << "DelegateIdentityManager::ChangeActivation - " << SleeveResultToString(ret_code);
        return ret_code;
    }

    // an epoch number of 0 indicates immediate settings change
    if (!epoch_num)
    {
        // Change activation status, reset activation schedule
        _activated[QueriedEpoch::Current] = _activated[QueriedEpoch::Next] = activate;
        _activation_schedule.start_epoch = epoch_num;
        LOG_DEBUG(_log) << "DelegateIdentityManager::ChangeActivation - changing activation status to "
                        << activate << " immediately";

        // Proceed to activate consensus components if Sleeved
        if (IsSleeved())
        {
            if (activate)
            {
                _node.ActivateConsensus();
            }
            else
            {
                _node.DeactivateConsensus();
            }
        }
    }
    // schedule
    else
    {
        // something is already scheduled in the future
        if (IsSettingChangeScheduled())
        {
            sleeve_code ret_code = sleeve_code::already_scheduled;
            LOG_WARN(_log) << "DelegateIdentityManager::ChangeActivation - " << SleeveResultToString(ret_code);
            return ret_code;
        }

        // scheduled epoch parameter must be for a future epoch. for immediate change, set to 0
        auto next_epoch_num = ConsensusContainer::GetCurEpochNumber() + 1;
        if (epoch_num < next_epoch_num)
        {
            sleeve_code ret_code = sleeve_code::invalid_setting_epoch;
            LOG_WARN(_log) << "DelegateIdentityManager::ChangeActivation - " << SleeveResultToString(ret_code);
            return ret_code;
        }

        // If the node is Sleeved, and we receive activation scheduling
        // between EpochTransitionEventsStart and EpochStart,
        // the scheduling command is rejected if it is for the immediate upcoming epoch.
        // The user is expected to manually activate / deactivate at this point.
        if (IsSleeved() && _node.GetEpochEventHandler()->TransitionEventsStarted() &&
            epoch_num == next_epoch_num)
        {
            sleeve_code ret_code = sleeve_code::epoch_transition_started;
            LOG_WARN(_log) << "DelegateIdentityManager::ChangeActivation - " << SleeveResultToString(ret_code);
            return ret_code;
        }

        // Update schedule; advertise if activated and in office next.
        _activation_schedule = {epoch_num, activate};
        if (epoch_num == next_epoch_num)
        {
            _activated[QueriedEpoch::Next] = activate;

            if (IsSleeved())
            {
                if (activate)
                {
                    uint8_t idx;
                    ApprovedEBPtr epoch_next;
                    IdentifyDelegates(ConsensusContainer::QueriedEpochToNumber(QueriedEpoch::Next),
                            idx, epoch_next);
                    if (idx != NON_DELEGATE)
                    {
                        auto ids = GetDelegatesToAdvertise(idx);
                        Advertise(next_epoch_num, idx, epoch_next, ids);
                        UpdateAddressAd(next_epoch_num, idx);
                    }
                }
                else
                {
                    // TODO: if already advertised for upcoming epoch, manually advertise deletion
                }
            }
        }

        LOG_DEBUG(_log) << "DelegateIdentityManager::ChangeActivation - changing activation status to "
                        << activate << " at future epoch " << epoch_num;
    }
    return sleeve_code::success;
}

sleeve_status
DelegateIdentityManager::CancelActivationScheduling()
{
    std::lock_guard<std::mutex> lock(_activation_mutex);

    if (!IsSettingChangeScheduled())
    {
        sleeve_code ret_code = sleeve_code::nothing_scheduled;
        LOG_WARN(_log) << "DelegateIdentityManager::CancelScheduling - " << SleeveResultToString(ret_code);
        return ret_code;
    }

    auto cur_epoch_number = ConsensusContainer::GetCurEpochNumber();
    if (_activation_schedule.start_epoch == cur_epoch_number + 1)
    {
        // If we are Sleeved and receive activation scheduling between EpochTransitionEventsStart and EpochStart,
        // the scheduling command is rejected if it is for the immediate upcoming epoch (check above).
        // The user is expected to manually activate / deactivate at this point.
        if (IsSleeved() && _node.GetEpochEventHandler()->TransitionEventsStarted())
        {
            sleeve_code ret_code = sleeve_code::epoch_transition_started;
            LOG_WARN(_log) << "DelegateIdentityManager::CancelActivationScheduling - " << SleeveResultToString(ret_code);
            return ret_code;
        }

        // Edge case: if previously scheduled for deactivation in the next epoch
        // and we are past the advertisement time, manually advertise
        if (!_activation_schedule.activate && IsSleeved() &&
            ArchivalTimer::GetNextEpochTime(_store.is_first_epoch() || _node.GetRecallHandler().IsRecall()) <=
                (AD_TIMEOUT_1 + TIMEOUT_SPREAD))
        {
            ApprovedEBPtr epoch_current;
            uint8_t idx;
            IdentifyDelegates(ConsensusContainer::QueriedEpochToNumber(QueriedEpoch::Current), idx, epoch_current);
            AdvertiseAndUpdateDB(cur_epoch_number, idx, epoch_current);
        }
    }

    LOG_DEBUG(_log) << "DelegateIdentityManager::CancelScheduling - Cancelled "
                    << (_activation_schedule.activate ? "" : "de") << "activation previously scheduled at epoch "
                    << _activation_schedule.start_epoch;

    // Clear activation schedule
    _activation_schedule.start_epoch = 0;

    return sleeve_code::success;
}

bool
DelegateIdentityManager::IsActiveInEpoch(QueriedEpoch queried_epoch)
{
    if (!IsSleeved())
    {
        return false;
    }

    return _activated[queried_epoch];
}

void
DelegateIdentityManager::ApplyActivationSchedule()
{
    /// ----------|<-EpochStart
    /// ----------||<-increment current epoch number
    /// ----------|||<-ApplyActivationSchedule()

    // First apply new setting
    _activated[QueriedEpoch::Current] = _activated[QueriedEpoch::Next];

    // Then scheduled change for this epoch
    auto cur_epoch = ConsensusContainer::GetCurEpochNumber();
    if (cur_epoch == _activation_schedule.start_epoch)
    {
        // Sanity check: in the previous epoch, the scheduled epoch would have been "Next"
        assert(_activated[QueriedEpoch::Next] == _activation_schedule.activate);
        _activation_schedule.start_epoch = 0;  // reset schedule (although not necessary)
    }
    // scheduled change for next epoch
    else if (cur_epoch + 1 == _activation_schedule.start_epoch)
    {
        _activated[QueriedEpoch::Next] = _activation_schedule.activate;
    }
    // schedule for next epoch remains unchanged if nothing is scheduled
    // or scheduled more than one epoch into the future
}

void
DelegateIdentityManager::IdentifyDelegates(
    QueriedEpoch queried_epoch,
    uint8_t &delegate_idx)
{
    ApprovedEBPtr epoch;
    IdentifyDelegates(queried_epoch, delegate_idx, epoch);
}

void
DelegateIdentityManager::IdentifyDelegates(
    QueriedEpoch queried_epoch,
    uint8_t &delegate_idx,
    ApprovedEBPtr &epoch)
{
    delegate_idx = NON_DELEGATE;

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

    bool stale_epoch = StaleEpoch(*epoch);
    // requested epoch block is not created yet
    if (stale_epoch && queried_epoch == QueriedEpoch::Next)
    {
        LOG_ERROR(_log) << "DelegateIdentityManager::IdentifyDelegates delegates set is requested for next epoch but epoch is stale";
        return;
    }

    if (!stale_epoch && queried_epoch == QueriedEpoch::Current)
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

    if (!IsSleeved())
    {
        LOG_WARN(_log) << "DelegateIdentityManager::IdentifyDelegates - Not currently Sleeved.";
        return;
    }

    DelegatePubKey own_pub;
    _bls_key->pub.serialize(own_pub);

    // Is this delegate included in the current/next epoch consensus?
    for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
    {
        // update delegates for the requested epoch
        if (epoch->delegates[del].bls_pub == own_pub)
        {
            assert (epoch->delegates[del].ecies_pub == _ecies_key->pub);
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

    auto get = [this, &epoch_number](BlockHash &hash, ApprovedEBPtr epoch) {
        if (_store.epoch_get(hash, *epoch))
        {
            if (hash != 0) {
                LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch: "
                                << hash.to_string();
                trace_and_halt();
            }
            return false;
        }
        // If we have gone past an epoch with a lower epoch number, we know the queried number won't be found
        return epoch->epoch_number >= epoch_number;
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
        LOG_DEBUG(_log) << "DelegateIdentityManager::IdentifyDelegates retrieving delegates from epoch "
                        << epoch->epoch_number;

        // Is this delegate included in the current/next epoch consensus?
        if (IsSleeved())
        {
            DelegatePubKey own_pub;
            _bls_key->pub.serialize(own_pub);

            for (uint8_t del = 0; del < NUM_DELEGATES; ++del) {
                // update delegates for the requested epoch
                if (epoch->delegates[del].bls_pub == own_pub)
                {
                    assert (epoch->delegates[del].ecies_pub == _ecies_key->pub);
                    delegate_idx = del;
                    break;
                }
            }
        }
        else
        {
            LOG_WARN(_log) << "DelegateIdentityManager::IdentifyDelegates - Not currently Sleeved.";
        }
    }
    else
    {
        LOG_DEBUG(_log) << "DelegateIdentityManager::IdentifyDelegates - epoch block number "
                        << epoch_number << " not found";
    }

    return found;
}

bool
DelegateIdentityManager::StaleEpoch(ApprovedEB & epoch)
{
    auto cur_epoch_num (ConsensusContainer::GetCurEpochNumber());
    assert(epoch.epoch_number < cur_epoch_num);
    return epoch.epoch_number + 1 != cur_epoch_num;
}

bool
DelegateIdentityManager::StaleEpoch()
{
    auto now_msec = GetStamp();
    auto rem = Milliseconds(now_msec % TConvert<Milliseconds>(EPOCH_PROPOSAL_TIME).count());
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

    if (StaleEpoch(epoch))
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
    if (IsActiveInEpoch(QueriedEpoch::Next))
    {
        IdentifyDelegates(ConsensusContainer::QueriedEpochToNumber(QueriedEpoch::Next), idx, epoch_next);
        AdvertiseAndUpdateDB(current_epoch_number + 1, idx, epoch_next);
    }

    // advertise for current epoch
    if (advertise_current && IsActiveInEpoch(QueriedEpoch::Current))
    {
        IdentifyDelegates(ConsensusContainer::QueriedEpochToNumber(QueriedEpoch::Current), idx, epoch_current);
        AdvertiseAndUpdateDB(current_epoch_number, idx, epoch_current);
    }

    ScheduleAd();
}

void
DelegateIdentityManager::P2pPropagate(
    uint32_t epoch_number,
    uint8_t delegate_id,
    std::shared_ptr<std::vector<uint8_t>> buf)
{
    bool res = _node.P2pPropagateMessage(buf->data(), buf->size(), true);
    LOG_DEBUG(_log) << "DelegateIdentityManager::Advertise, " << (res?"propagating":"failed")
                    << ": epoch number " << epoch_number
                    << ", delegate id " << (int) delegate_id
                    << ", ip " << _node.GetConfig().consensus_manager_config.local_address
                    << ", port " << _node.GetConfig().consensus_manager_config.peer_port
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
    {
        std::lock_guard<std::mutex> lock(_activation_mutex);
        IdentifyDelegates(CurToDelegatesEpoch(epoch_number), idx, eb);
    }
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
    // Advertise to other delegates this delegate's ip
    const ConsensusManagerConfig & cmconfig = _node.GetConfig().consensus_manager_config;
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
    const TxAcceptorConfig &txconfig = _node.GetConfig().tx_acceptor_config;
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

void DelegateIdentityManager::AdvertiseAndUpdateDB(
    const uint32_t & epoch_number,
    const uint8_t & delegate_id,
    std::shared_ptr<ApprovedEB> epoch)
{
    if (delegate_id == NON_DELEGATE)
    {
        return;
    }

    auto ids = GetDelegatesToAdvertise(delegate_id);
    Advertise(epoch_number, delegate_id, epoch, ids);
    UpdateAddressAd(epoch_number, delegate_id);

    LOG_INFO(_log) << "DelegateIdentityManager::AdvertiseAndUpdateDB - advertised and updated DB as delegate with index "
                   << (int)delegate_id << " for epoch number " << epoch_number;
}

void
DelegateIdentityManager::Decrypt(const std::string &cyphertext, uint8_t *buf, size_t size)
{
    _ecies_key->prv.Decrypt(cyphertext, buf, size);
}

bool
DelegateIdentityManager::OnAddressAd(uint8_t *data,
                                     size_t size,
                                     const PrequelAddressAd &prequel,
                                     std::string &ip,
                                     uint16_t &port)
{
    auto current_epoch_number = ConsensusContainer::GetCurEpochNumber();
    bool current_or_next = prequel.epoch_number == current_epoch_number ||
                           prequel.epoch_number == (current_epoch_number + 1);

    if (!current_or_next)
        return false;

    uint8_t idx = GetDelegateIdFromCache(prequel.epoch_number);  // _activation_mutex locked by caller

    // return false (do not proceed) if ad is not intended for this delegate (not encrypted with our ECIES public key)
    if (prequel.encr_delegate_id != idx)
        return false;

    // don't update if already have it
    if (_address_ad.find({prequel.epoch_number, prequel.delegate_id}) != _address_ad.end())
    {
        // Retrieve from cache
        ip = _address_ad[{prequel.epoch_number, prequel.delegate_id}].ip;
        port = _address_ad[{prequel.epoch_number, prequel.delegate_id}].port;
        LOG_DEBUG(_log) << "DelegateIdentityManager::OnAddressAd - ad already in cache; epoch " << prequel.epoch_number
                        << " delegate id " << (int)prequel.delegate_id
                        << " encr delegate id " << (int)prequel.encr_delegate_id
                        << " store ip " << ip << "stored port " << port;
        return true;
    }

    LOG_DEBUG(_log) << "DelegateIdentityManager::OnAddressAd, epoch " << prequel.epoch_number
                    << " delegate id " << (int)prequel.delegate_id
                    << " encr delegate id " << (int)prequel.encr_delegate_id
                    << " size " << size;

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

    UpdateAddressAdDB(prequel, data, size);

    return true;
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
    LOG_DEBUG(_log) << "DelegateIdentityManager::UpdateAddressAdDB - added address ad; "
                       "epoch number " << prequel.epoch_number
                    << " delegate id " << (int)prequel.delegate_id
                    << " encr delegate id " << (int)prequel.encr_delegate_id;
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
    auto & config = _node.GetConfig().consensus_manager_config;
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

bool
DelegateIdentityManager::IsSleeved()
{
    return _bls_key && _ecies_key;
}

void
DelegateIdentityManager::OnSleeved(logos::transaction const & tx)
{
    // retrieve BLS and ECIES keypairs from Sleeve database, and enter Sleeved state
    // TODO: benchmark relative performance loss of storing governance keys using fan-out in memory
    _bls_key = _sleeve.GetBLSKey(tx);
    assert (_bls_key);
    _ecies_key = _sleeve.GetECIESKey(tx);
    assert (_ecies_key);

    // load advertisement messages to self
    LoadDBAd2Self();

    // Check for activation scheduling
    // if activated now, start all consensus components, and advertise immediately
    if (IsActiveInEpoch(QueriedEpoch::Current))
    {
        LOG_DEBUG(_log) << "DelegateIdentityManager::OnSleeved - Activated Current, Activating consensus now";
        _node.ActivateConsensus();
        // ConsensusContainer::ActivateConsensus() handles the case where the node is active currently but not next
    }
    else if (IsActiveInEpoch(QueriedEpoch::Next))
    {
        LOG_DEBUG(_log) << "DelegateIdentityManager::OnSleeved - Activated Next, setting up for upcoming epoch";
        // If already Transitioning, we may need to set up now (change transition delegate type and build EpochManager)
        _node.GetEpochEventHandler()->UpcomingEpochSetUp();
    }
    LOG_DEBUG(_log) << "DelegateIdentityManager::OnSleeved - completed Sleeving setup";
}

void DelegateIdentityManager::OnUnsleeved()
{
    std::lock_guard<std::mutex> lock(_activation_mutex);
    _node.DeactivateConsensus();

    // TODO: zero the keys' content first
    _bls_key = nullptr;
    _ecies_key = nullptr;
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
        assert(sizeof(adKey) == it->first.size());
        memcpy(&adKey, it->first.data(), it->first.size());
        if (adKey.epoch_number < current_epoch_number)
        {
            ad2del.push_back(adKey);
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
DelegateIdentityManager::LoadDBAd2Self()
{
    LOG_DEBUG(_log) << "DelegateIdentityManager::LoadDBAd2Self - beginning scan of database";
    logos::transaction transaction (_store.environment, nullptr, true);

    logos::block_store::ad_key adKey;
    logos::block_store::ad_txa_key adTxaKey;

    for (auto it = logos::store_iterator(transaction, _store.address_ad_db);
         it != logos::store_iterator(nullptr);
         ++it)
    {
        bool error (false);
        if (sizeof(adKey) != it->first.size())
        {
            // delete and continue
            LOG_WARN(_log) << "DelegateIdentityManager::LoadDBAd2Self - detected corrupted database value";
            assert(!it.delete_current_record());
            continue;
        }
        memcpy(&adKey, it->first.data(), it->first.size());

        /// all ad messages are saved to the database even if they are encrypted
        /// with another delegate id so that the delegate can respond to peer request
        /// for ad messages. we only store in memory messages encrypted with this delegate id
        auto idx = GetDelegateIdFromCache(adKey.epoch_number);  // _activation_mutex should be locked by caller
        LOG_DEBUG(_log) << "DelegateIdentityManager::LoadDBAd2Self - delegate idx is " << (unsigned)idx;
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
                    LOG_DEBUG(_log) << "DelegateIdentityManager::LoadDBAd2Self, ad epoch_number " << ad.epoch_number
                                    << " delegate id " << (int) ad.delegate_id
                                    << " ip " << ip << " port " << ad.port;
                }
            }
            catch (const std::exception &e) {
                LOG_ERROR(_log) << "DelegateIdentityManager::LoadDBAd2Self, failed: " << e.what();
            }
        }
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
DelegateIdentityManager::TxAValidateDelegate(std::shared_ptr<Socket> socket,
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
    auto lapse = ArchivalTimer::GetNextEpochTime(_store.is_first_epoch() || _node.GetRecallHandler().IsRecall());

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
    std::lock_guard<std::mutex> lock(_ad_timer_mutex);
    std::weak_ptr<DelegateIdentityManager> this_w =
        shared_from_this();
    _timer.expires_from_now(msec);
    _timer.async_wait([this_w](const ErrorCode &ec){
        auto this_s = GetSharedPtr(this_w, "DelegateIdentityManager::ScheduleAd - object destroyed");
        if (!this_s)
        {
            return;
        }
        this_s->Advert(ec);
    });
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
DelegateIdentityManager::OnTxAcceptorUpdate(QueriedEpoch queried_epoch,
                                            std::string &ip,
                                            uint16_t port,
                                            uint16_t bin_port,
                                            uint16_t json_port,
                                            bool add)
{
    uint8_t idx = NON_DELEGATE;
    ApprovedEBPtr eb;

    IdentifyDelegates(ConsensusContainer::QueriedEpochToNumber(queried_epoch), idx, eb);
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
    Sign(ad.Hash(), ad.signature);
    auto buf = std::make_shared<std::vector<uint8_t>>();
    ad.Serialize(*buf);

    UpdateTxAcceptorAdDB(ad, buf->data(), buf->size());

    P2pPropagate(current_epoch_number, idx, buf);

    return _node.UpdateTxAcceptor(ip, port, add);
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
    ad.Serialize(buf, _ecies_key->pub);
    UpdateAddressAdDB(ad, buf.data(), buf.size());
}

void
DelegateIdentityManager::UpdateAddressAd(uint32_t epoch_number, uint8_t delegate_id)
{
    auto &config = _node.GetConfig().consensus_manager_config;
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
        if (idx != NON_DELEGATE)
        {
            _idx_cache.emplace(cur_epoch_number, idx);
            if (_idx_cache.size() > MAX_CACHE_SIZE) {
                _idx_cache.erase(_idx_cache.begin());
            }
        }
        return idx;
    }
}

GenesisBlock::GenesisBlock()
{}

bool GenesisBlock::deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
{
    digest = BlockHash(0);
    blake2b_state hash;

    auto status(blake2b_init(&hash, sizeof(BlockHash)));
    assert(status == 0);

    std::stringstream ss;
    boost::property_tree::json_parser::write_json(ss, tree_a);
    boost::property_tree::ptree::const_iterator end;
    int idx = 0;
    try
    {
        auto accnts(tree_a.get_child("accounts"));
        end = accnts.end();
        for (boost::property_tree::ptree::const_iterator it = accnts.begin(); it != end; ++it)
        {
            AccountAddress account;
            account.decode_hex(it->second.get<std::string>("account"));
            Amount amount;
            amount.decode_dec(it->second.get<std::string>("amount"));
            BlockHash previous;
            previous.decode_hex(it->second.get<std::string>("previous"));
            uint32_t sequence = it->second.get<uint32_t>("sequence");
            AccountSig sendsig;
            sendsig.decode_hex(it->second.get<std::string>("signature"));

            gen_sends[idx] = Send(logos::logos_test_account,   // account
                                  previous,                    // previous
                                  sequence,                    // sequence
                                  account,                     // link/to
                                  amount,                      // amount
                                  0,                           // transaction fee
                                  sendsig);                    // signature

            gen_sends[idx].Hash(hash);
            idx++;
        }
    }
    catch (std::runtime_error const &)
    {
        LOG_ERROR(_log) << "GenesisBlock::deserialize_json - failed deserializing Genesis Sends";
        return false;
    }

    try
    {
        // Deserialize microblocks from config
        auto micro(tree_a.get_child("micros"));
        idx = 0;
        end = micro.end();
        for (boost::property_tree::ptree::const_iterator it = micro.begin(); it != end; ++it)
        {
            gen_micro[idx].epoch_number = it->second.get<uint32_t>("epoch_number");
            gen_micro[idx].sequence = it->second.get<uint32_t>("sequence");
            gen_micro[idx].timestamp = 0;
            gen_micro[idx].previous.decode_hex(it->second.get<std::string>("previous"));
            gen_micro[idx].last_micro_block = 1;
            gen_micro[idx].Hash(hash);
            idx++;
        }
    }
    catch (std::runtime_error const &)
    {
        LOG_ERROR(_log) << "GenesisBlock::deserialize_json - failed deserializing Genesis Microblocks";
        return false;
    }

    try
    {
        // Deserialize epochs from config
        auto epoch(tree_a.get_child("epochs"));
        idx = 0;
        end = epoch.end();
        for (boost::property_tree::ptree::const_iterator it = epoch.begin(); it != end; ++it)
        {
            gen_epoch[idx].epoch_number = it->second.get<uint32_t>("epoch_number");
            gen_epoch[idx].sequence = 0;
            gen_epoch[idx].timestamp = 0;
            gen_epoch[idx].total_RBs = 0;
            gen_epoch[idx].micro_block_tip = gen_micro[idx].CreateTip();
            gen_epoch[idx].previous.decode_hex(it->second.get<std::string>("previous"));
            auto delegates(it->second.get_child("delegates"));
            int del_idx = 0;
            boost::property_tree::ptree::const_iterator end1 = delegates.end();
            for (boost::property_tree::ptree::const_iterator it1 = delegates.begin(); it1 != end1; ++it1)
            {
                logos::public_key pub;
                pub.decode_hex(it1->second.get<std::string>("account"));
                DelegatePubKey dpk(it1->second.get<std::string>("bls_pub"));
                ECIESPublicKey ecies_key;
                ecies_key.FromHexString(it1->second.get<std::string>("ecies_pub"));
                Amount stake, vote;
                stake.decode_dec(it1->second.get<std::string>("stake"));
                vote.decode_dec(it1->second.get<std::string>("vote"));
                Delegate delegate = {pub, dpk, ecies_key, stake, stake};
                delegate.starting_term = false;
                gen_epoch[idx].delegates[del_idx] = delegate;
                del_idx++;
            }
            gen_epoch[idx].Hash(hash);
            idx++;
        }
    }
    catch (std::runtime_error const &)
    {
        LOG_ERROR(_log) << "GenesisBlock::deserialize_json - failed deserializing Genesis Epochs";
        return false;
    }

    try
    {

        // Deserialize StartRepresenting requests for genesis delegates from config
        auto starts(tree_a.get_child("start"));
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
    }
    catch (std::runtime_error const &)
    {
        LOG_ERROR(_log) << "GenesisBlock::deserialize_json - failed deserializing Genesis StartRepresenting";
        return false;
    }

    try
    {
        // Deserialize AnnounceCandidacy requests for genesis delegates from config
        auto announces(tree_a.get_child("announce"));
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
            announce[idx].set_stake = true;
            announce[idx].ecies_key.FromHexString(it->second.get<std::string>("ecies_pub"));
            DelegatePubKey dpk(it->second.get<std::string>("bls_pub"));
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
    }
    catch (std::runtime_error const &)
    {
        LOG_ERROR(_log) << "GenesisBlock::deserialize_json - failed deserializing Genesis AnnounceCandidacy";
        return false;
    }

    try
    {
        // Get signature from config
        signature.decode_hex(tree_a.get<std::string>("signature"));
        status = blake2b_final(&hash, digest.data(), sizeof(BlockHash));
        assert(status == 0);
    }
    catch (std::runtime_error const &)
    {
        LOG_ERROR(_log) << "GenesisBlock::deserialize_json - failed deserializing Genesis Signature";
        return false;
    }

    return true;
}

bool GenesisBlock::VerifySignature(AccountPubKey const & pub) const
{
    return 0 == ed25519_sign_open(digest.data(),
                                  HASH_SIZE,
                                  pub.data(),
                                  signature.data());
}

// TODO: include validate
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

