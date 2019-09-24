#include <gtest/gtest.h>
#include <logos/consensus/epoch_manager.hpp>
#include <logos/node/node.hpp>
#include <logos/identity_management/delegate_identity_manager.hpp>

#include <boost/dll.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>

const Milliseconds START_DELAY (1000);
const std::string ecies_prv_str = "ccc3cdefdef6fe4c5ce4c2282b0d89d097c58ea5de5bd43aec5f6a2691d4a8d7";
const std::string bls_prv_str = "07E49AD8F920C93F98499D440B60AAAD2D1AFA31A0747E7BEB6915341730411D";

/// Mock class for node
namespace logos
{
    class NodeInterface;
}
class EpochManager;

/// While running the tests below, make sure to modify the various interval values
/// in epoch_time_util.hpp and delegate_identity_manager.hpp
/// to ensure tests finish in a reasonable amount of time.
/// Example values are:
///     (epoch_time_util.hpp)
///     static const Seconds EPOCH_DELEGATES_CONNECT(3);
///     static const Seconds EPOCH_TRANSITION_START(1);
///     static const Seconds EPOCH_PROPOSAL_TIME(5);
///     static const Seconds EPOCH_TRANSITION_END(1);
///     static const Seconds MICROBLOCK_PROPOSAL_TIME(1);
///     static const Seconds MICROBLOCK_CUTOFF_TIME(1);
///     (delegate_identity_manager.hpp)
///     static constexpr Seconds AD_TIMEOUT_1{4};
///     static constexpr Seconds AD_TIMEOUT_2{3};
///     static constexpr Seconds TIMEOUT_SPREAD{1};

class TestNode : public logos::NodeInterface
{
public:
    TestNode(logos::node_init & init, boost::asio::io_service & service, logos::node_config const & config, boost::filesystem::path const & application_path)
        : _service(service)
        , _alarm(_service)
        , _config(config)
        , _store(init.block_store_init, application_path / "data.ldb", _config.lmdb_max_dbs)
        , _block_cache(service, _store)
        , _application_path(application_path)
        , _recall_handler()
        , _p2p()
        , _sleeve(application_path / "sleeve.ldb", _config.password_fanout, init.block_store_init)
        , _identity_manager{std::make_shared<DelegateIdentityManager>(*this, _store, _service, _sleeve)}
        , _consensus_container{std::make_shared<ConsensusContainer>(
                _service, _store, _block_cache, _alarm, _config, _recall_handler, *_identity_manager, _p2p)}
    {}

    ~TestNode() override
    {
        {
            logos::transaction tx(_sleeve._env, nullptr, true);
            mdb_drop(tx, _sleeve._sleeve_handle, 0);
        }
        {
            logos::transaction tx(_store.environment, nullptr, true);
            _store.clear(_store.candidacy_db, tx);
            _store.clear(_store.representative_db, tx);
            _store.clear(_store.epoch_db, tx);
            _store.clear(_store.epoch_tip_db, tx);
            _store.clear(_store.remove_candidates_db, tx);
            _store.clear(_store.remove_reps_db, tx);
            _store.clear(_store.leading_candidates_db, tx);
            _store.clear(_store.voting_power_db, tx);
            _store.clear(_store.staking_db, tx);
            _store.clear(_store.thawing_db, tx);
            _store.clear(_store.master_liabilities_db, tx);
            _store.clear(_store.secondary_liabilities_db, tx);
            _store.clear(_store.rep_liabilities_db, tx);
            _store.clear(_store.rewards_db, tx);
            _store.clear(_store.global_rewards_db, tx);
            _store.clear(_store.delegate_rewards_db, tx);
            _store.clear(_store.account_db, tx);
            _store.leading_candidates_size = 0;
        }
        LOG_DEBUG(_log) << "~TestNode - dropped all db's.";
        _identity_manager->CancelAdvert();
        DelegateMap::instance = nullptr;
        _consensus_container->DeactivateConsensus();
        _service.stop();
    }

    void ActivateConsensus() override
    {
        _consensus_container->ActivateConsensus();
    }

    void DeactivateConsensus() override
    {
        _consensus_container->DeactivateConsensus();
    }

    const logos::node_config & GetConfig() override
    {
        return _config;
    }

    std::shared_ptr<NewEpochEventHandler> GetEpochEventHandler() override
    {
        return _consensus_container;
    }

    IRecallHandler & GetRecallHandler() override
    {
        return _recall_handler;
    }

    bool P2pPropagateMessage(const void *message, unsigned size, bool output) override
    {
        return true;
    }

    bool UpdateTxAcceptor(const std::string &ip, uint16_t port, bool add) override
    {
        return true;
    }

    boost::filesystem::path const & GetApplicationPath() override {return _application_path;}

    boost::asio::io_service &                   _service;
    logos::alarm                                _alarm;
    logos::node_config                          _config;
    logos::block_store                          _store;
    logos::BlockCache                           _block_cache;
    boost::filesystem::path                     _application_path;
    RecallHandler                               _recall_handler;
    p2p_interface                               _p2p;                 ///< stub placeholder
    Sleeve                                      _sleeve;
    std::shared_ptr<DelegateIdentityManager>    _identity_manager;
    std::shared_ptr<ConsensusContainer>         _consensus_container;
    Log                                         _log;
};

class TestTimeUtil : public TimeUtil
{
public:
    ~TestTimeUtil() override = default;

    Milliseconds GetNextMicroBlockTime(uint8_t skip) override
    {
        return GetNextTime(MICROBLOCK_PROPOSAL_TIME, _mb_offset, skip);
    }

    Milliseconds GetNextEpochTime(uint8_t skip) override
    {
        return GetNextTime(EPOCH_PROPOSAL_TIME, _eb_offset, skip);
    }

    void SetFakeEpochOffset(const uint64_t & t)
    {
        Log log;
        LOG_DEBUG(log) << "SetTestTimeUtil - offset is " << t;
        _eb_offset = t % TConvert<Milliseconds>(EPOCH_PROPOSAL_TIME).count();
        _mb_offset = t % TConvert<Milliseconds>(MICROBLOCK_PROPOSAL_TIME).count();
    }

private:
    Milliseconds GetNextTime(Seconds timeout, const uint64_t & offset, uint8_t skip)
    {
        auto now = GetStamp();
        auto timeout_msec = TConvert<Milliseconds>(timeout).count();
        auto rem = (now - offset) % timeout_msec;
        return Milliseconds((rem ? timeout_msec - rem : 0) + skip * timeout_msec);
    }

    uint64_t _eb_offset = 0;
    uint64_t _mb_offset = 0;
};

std::shared_ptr<TestNode> CreateTestNode(boost::asio::io_service & service)
{
    auto path (boost::dll::program_location().parent_path());
    auto remove ("rm -rf " + (path / "*.ldb").string());
    system(remove.c_str());
    logos::node_init init;
    logos::node_config config;
    // generate placeholder configuration
    std::string local_ip ("127.0.0.1");
    config.consensus_manager_config.local_address = local_ip;
    config.consensus_manager_config.peer_port = 60000;
    config.consensus_manager_config.enable_epoch_transition = true;
    config.consensus_manager_config.enable_elections = true;
    config.tx_acceptor_config.delegate_ip = local_ip;
    config.tx_acceptor_config.acceptor_ip = local_ip;

    // set logging level
    boost::log::add_common_attributes ();
    boost::log::register_simple_formatter_factory< boost::log::trivial::severity_level, char > ("Severity");
    boost::log::core::get()->set_filter (
            boost::log::trivial::severity >= boost::log::trivial::trace
    );

    // create node smart pointer
    auto node (std::make_shared<TestNode>(init, service, config, path));
    if (!init.error())
    {
        return node;
    }
    else
    {
        Log log;
        LOG_FATAL(log) << "CreateTestNode - Error creating TestNode.";
        trace_and_halt();
    }

    return std::shared_ptr<TestNode>();
}

void SetTestTimeUtil(uint64_t addn_offset = 0)
{
    auto util_instance = std::make_shared<TestTimeUtil>();
//    util_instance->SetFakeEpochOffset(GetStamp() + addn_offset);
    ArchivalTimer::_util_instance = util_instance;
}

std::shared_ptr<TestNode> PrepNewNode(
    std::unique_ptr<logos::thread_runner> & runner,
    boost::asio::io_service & service,
    bool set_time)
{
    if (set_time)
    {
        SetTestTimeUtil(START_DELAY.count());
    }
    auto node = CreateTestNode(service);
    node->_consensus_container->Start();
    // sanity checks at creation
    bool cond = (node->_sleeve.IsUnlocked()) &&
                (node->_consensus_container->GetTransitionState() == EpochTransitionState::None) &&
                (node->_consensus_container->GetTransitionDelegate() == EpochTransitionDelegate::None) &&
                (!node->_identity_manager->GetActivationStatus(QueriedEpoch::Current)) &&
                (!node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
    if (cond)
    {
        if (service.stopped())
        {
            service.restart();
        }
        runner = std::make_unique<logos::thread_runner> (node->_service, 4);
        return node;
    }
    else
    {
        return nullptr;
    }
}

TEST (IdentityManagement, ImmediateActivation)
{

    std::unique_ptr<logos::thread_runner> runner;
    boost::asio::io_service service;

    /// 1. Sleeve then activate before EpochTransitionEventsStart (ETES)
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve then activate before ETES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);
        LOG_DEBUG(node->_log) << __func__ << " - Sleep: lapse=" << lapse.count()
                    << ", connect time=" << TConvert<Milliseconds>(EPOCH_DELEGATES_CONNECT).count();

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Activate (immediate)
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
        }

        // Sleep till epoch transition events start for epochs 3 ==> 4
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Check ETES states
        LOG_DEBUG(node->_log) << __func__ << " - current time: " << GetStamp()
                    << ", state=" << TransitionStateToName(node->_consensus_container->GetTransitionState())
                    << ", delegate=" << TransitionDelegateToName(node->_consensus_container->GetTransitionDelegate());
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())->GetConnection(), EpochConnection::Current);
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)->GetConnection(), EpochConnection::Transitioning);
        }

        // Check ETS
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())->GetConnection(), EpochConnection::Current);
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)->GetConnection(), EpochConnection::Transitioning);
        }

        // Check ES
        std::this_thread::sleep_for(EPOCH_TRANSITION_START);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() - 1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)->GetConnection(), EpochConnection::WaitingDisconnect);
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())->GetConnection(), EpochConnection::Transitioning);
        }

        // Check ETE
        std::this_thread::sleep_for(EPOCH_TRANSITION_END);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())->GetConnection(), EpochConnection::Current);
        }

        std::this_thread::sleep_for(Milliseconds(50));  // TODO: this is a hacky way to avoid segfault, same below
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve then activate before ETES";
    }

    /// 2. Activate then sleeve before ETES
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Activate then sleeve before ETES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);
        LOG_DEBUG(node->_log) << __func__ << " - Sleep: lapse=" << lapse.count()
                    << ", connect time=" << TConvert<Milliseconds>(EPOCH_DELEGATES_CONNECT).count();

        // Activate (immediate)
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
        }

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
        }

        // Sleep till epoch transition events start for epochs 3 ==> 4
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Check ETES states
        LOG_DEBUG(node->_log) << __func__ << " - current time: " << GetStamp()
                    << ", state=" << TransitionStateToName(node->_consensus_container->GetTransitionState())
                    << ", delegate=" << TransitionDelegateToName(node->_consensus_container->GetTransitionDelegate());
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);

        // Check ETS
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Activate then sleeve before ETES";
    }

    /// 3. Sleeve before ETES, Activate between ETES and ETS
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve before ETES, Activate between ETES and ETS";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);
        LOG_DEBUG(node->_log) << __func__ << " - Sleep: lapse=" << lapse.count()
                              << ", connect time=" << TConvert<Milliseconds>(EPOCH_DELEGATES_CONNECT).count();

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Sleep till ETES for epochs 3 ==> 4
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Check ETES states
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            LOG_DEBUG(node->_log) << __func__ << " - current time: " << GetStamp()
                                  << ", state=" << TransitionStateToName(node->_consensus_container->GetTransitionState())
                                  << ", delegate=" << TransitionDelegateToName(node->_consensus_container->GetTransitionDelegate());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
        }

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            // Check EpochManager creation
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
        }

        // Check ETS
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve before ETES, Activate between ETES and ETS";
    }

    /// 4. Sleeve before ETES, Activate between ETS and ES
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve before ETES, Activate between ETS and ES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Sleep till ETES for epochs 3 ==> 4
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Check ETES states
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleep till ETS
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
        }

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Check EpochManager creation
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
        }

        // Check ES
        std::this_thread::sleep_for(EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve before ETES, Activate between ETS and ES";
    }

    /// 5. Sleeve before ETES, Activate between ES and ETE
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve before ETES, Activate between ES and ETE";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Sleep till ETES for epochs 3 ==> 4
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Check ETES states
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Check ETS states
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Check ES
        std::this_thread::sleep_for(EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
        // Check EpochManager Creation
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
            // TransitionDelegate should still be none since activation took place after ES
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
        }

        // Check ETE
        std::this_thread::sleep_for(EPOCH_TRANSITION_END);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve before ETES, Activate between ES and ETE";
    }

    /// 6. Sleeve before ETES, Activate after ETE
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve before ETES, Activate after ETE";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Sleep till ETES for epochs 3 ==> 4
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Check ETES states
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Check ETS states
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Check ES
        std::this_thread::sleep_for(EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Check ETE
        std::this_thread::sleep_for(EPOCH_TRANSITION_END);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Check EpochManager Creation
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber() + 1)));
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve before ETES, Activate after ETE";
    }

    /// 7. Sleeve then activate between ETES and ETS
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve then activate between ETES and ETS";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        // Check ETS states
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve then activate between ETES and ETS";
    }

    /// 8. Activate before ETES, Sleeve between ETES and ETS
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Activate before ETES, Sleeve between ETES and ETS";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        // Check ETS states
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Activate before ETES, Sleeve between ETES and ETS";
    }

    /// 9. Activate then Sleeve between ETES and ETS
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Activate then sleeve between ETES and ETS";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        // Check ETS states
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Activate then sleeve between ETES and ETS";
    }

    /// 10. Sleeve between ETES and ETS, Activate between ETS and ES
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve between ETES and ETS, Activate between ETS and ES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Check ETS states
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve between ETES and ETS, Activate between ETS and ES";
    }

    /// 11. Sleeve between ETES and ETS, Activate between ES and ETE
    {
        {
        Log log;
        LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve between ETES and ETS, Activate between ES and ETE";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Check ES states
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve between ETES and ETS, Activate between ES and ETE";
    }

    /// Omitted: Sleeve between ETES and ETS, Activate after ETE

    /// 12. Sleeve then activate between ETS and ES
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve then activate between ETS and ES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        std::this_thread::sleep_for(lapse - EPOCH_TRANSITION_START + Milliseconds(10));

        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        // Check ES states
        std::this_thread::sleep_for(EPOCH_TRANSITION_START);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
        }

        // Check ETE states
        std::this_thread::sleep_for(EPOCH_TRANSITION_END);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve then activate between ETS and ES";
    }

    /// 13. Activate before ETES, Sleeve between ETS and ES
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Activate before ETES, Sleeve between ETS and ES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        std::this_thread::sleep_for(lapse - EPOCH_TRANSITION_START + Milliseconds(10));

        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Activate before ETES, Sleeve between ETS and ES";
    }

    /// 14. Activate between ETES and ETS, Sleeve between ETS and ES
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Activate between ETES and ETS, Sleeve between ETS and ES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Sleep till ETS
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT - EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Activate between ETES and ETS, Sleeve between ETS and ES";
    }

    /// 15. Activate then sleeve between ETS and ES
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Activate then sleeve between ETS and ES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleep till ETS
        std::this_thread::sleep_for(lapse - EPOCH_TRANSITION_START + Milliseconds(10));

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Activate then sleeve between ETS and ES";
    }

    /// 16. Sleeve between ETS and ES, Activate between ES and ETE
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve between ETS and ES, Activate between ES and ETE";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleep till ETS
        std::this_thread::sleep_for(lapse - EPOCH_TRANSITION_START + Milliseconds(10));

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Wait till ES
        std::this_thread::sleep_for(EPOCH_TRANSITION_START);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve between ETS and ES, Activate between ES and ETE";
    }

    /// 17. Sleeve between ETS and ES, Activate after ETE
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve between ETS and ES, Activate after ETE";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Wait till ETS
        std::this_thread::sleep_for(lapse - EPOCH_TRANSITION_START + Milliseconds(10));

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Wait till ETE
        std::this_thread::sleep_for(EPOCH_TRANSITION_START + EPOCH_TRANSITION_END);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve between ETS and ES, Activate after ETE";
    }

    /// 18. Sleeve and then Activate between ES and ETE
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve and then Activate between ES and ETE";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Wait till ETS
        std::this_thread::sleep_for(lapse - EPOCH_TRANSITION_START + Milliseconds(10));

        // Check ETS states
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Wait till ES
        std::this_thread::sleep_for(EPOCH_TRANSITION_START);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve and then Activate between ES and ETE";
    }

    /// 19. Activate before ETES, Sleeve between ES and ETE (some interval cases for Activation omitted here)
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Activate before ETES, Sleeve between ES and ETE";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Wait till ES
        std::this_thread::sleep_for(lapse + Milliseconds(10));

        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Activate before ETES, Sleeve between ES and ETE";
    }

    /// 20. Activate before ETES, Sleeve after ETE (some interval cases for Activation omitted here)
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Activate before ETES, Sleeve after ETE";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(node->_store.is_first_epoch());
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Wait till ETE
        std::this_thread::sleep_for(lapse + EPOCH_TRANSITION_END + Milliseconds(10));

        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Activate before ETES, Sleeve after ETE";
    }

    /// 21. Launching the node at [ETES, ETS), then Sleeve and Activate
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Launching the node at [ETES, ETS), then Sleeve and Activate";
        }
        SetTestTimeUtil(START_DELAY.count());
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Wait till ETES
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        auto node = PrepNewNode(runner, service, false);
        ASSERT_TRUE(bool(node));

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        lapse = ArchivalTimer::GetNextEpochTime(true);
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        // Wait till ES
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Launching the node at [ETES, ETS), then Sleeve and Activate";
    }
}

TEST (IdentityManagement, ScheduledActivation)
{
    boost::asio::io_service service;
    std::unique_ptr<logos::thread_runner> runner;

    /// 1. Sleeve before ETES, Schedule for activation during epoch transition 3==>4
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve before ETES, Schedule for activation during epoch transition 3==>4";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Schedule Activation for next epoch
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Wait till ETES
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(100));

        // Check ETES states
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        // Wait till ES
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve before ETES, Schedule for activation during epoch transition 3==>4";
    }

    /// 2. Schedule for activation during epoch transition 3==>4, Sleeve between [ETES, ETS)
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Schedule for activation during epoch transition 3==>4, Sleeve between [ETES, ETS)";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Schedule Activation for next epoch
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Wait till ETES
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Check ETES states pre-Sleeving
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Check ETES states post-Sleeving
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
        ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
        ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)->GetConnection(), EpochConnection::Transitioning);

        // Wait till ES
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())->GetConnection(), EpochConnection::Transitioning);
        }

        // Wait till ETE
        std::this_thread::sleep_for(EPOCH_TRANSITION_END);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())->GetConnection(), EpochConnection::Current);
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Schedule for activation during epoch transition 3==>4, Sleeve between [ETES, ETS)";
    }

    /// 3. Schedule for activation during epoch transition 3==>4, Sleeve between [ETS, ES)
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Schedule for activation during epoch transition 3==>4, Sleeve between [ETS, ES)";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Schedule Activation for next epoch
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Wait till ETS
        std::this_thread::sleep_for(lapse - EPOCH_TRANSITION_START + Milliseconds(10));

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Check ETS states post-Sleeving
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochTransitionStart);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)->GetConnection(), EpochConnection::Transitioning);
        }

        // Wait till ES
        std::this_thread::sleep_for(EPOCH_TRANSITION_START);
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Schedule for activation during epoch transition 3==>4, Sleeve between [ETS, ES)";
    }

    /// 4. Schedule for activation during epoch transition 3==>4, Sleeve right after ES
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Schedule for activation during epoch transition 3==>4, Sleeve right after ES";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Schedule Activation for next epoch
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Wait till ES
        std::this_thread::sleep_for(lapse + Milliseconds(10));

        // Check ES states pre-Sleeving
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
        ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::EpochStart);
        ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
        ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Check ES states post-Sleeving
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::None);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), NON_DELEGATE);
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())->GetConnection(), EpochConnection::Current);
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Schedule for activation during epoch transition 3==>4, Sleeve right after ES";
    }

    /// 5. Failure scenario: schedule for activation between [ETES, ES)
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Failure scenario: schedule for activation between [ETES, ES)";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Wait till ETES
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Schedule (invalid) Activation for next epoch
        ASSERT_EQ(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2), sleeve_status(sleeve_code::epoch_transition_started));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        // Wait till ES
        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT);
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Failure scenario: schedule for activation between [ETES, ES)";
    }

    /// 6. Failure scenario: invalid epoch / setting already applied / already scheduled; Cancellation before transition 3==>4
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Failure scenario: invalid epoch / setting already applied / already scheduled; Cancellation before transition 3==>4";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Schedule (invalid) Activation for old epoch
        ASSERT_EQ(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 1), sleeve_status(sleeve_code::invalid_setting_epoch));
        ASSERT_EQ(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH), sleeve_status(sleeve_code::invalid_setting_epoch));

        // Attempt to deactivate
        ASSERT_EQ(node->_identity_manager->ChangeActivation(false, GENESIS_EPOCH + 2), sleeve_status(sleeve_code::setting_already_applied));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Schedule activation then cancel
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 3));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
        ASSERT_TRUE(node->_identity_manager->CancelActivationScheduling());

        // Schedule activation
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Attempt to schedule again
        ASSERT_EQ(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2), sleeve_status(sleeve_code::already_scheduled));

        // Wait till ES
        std::this_thread::sleep_for(lapse + Milliseconds(10));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()-1)));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
            ASSERT_EQ(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())->GetConnection(), EpochConnection::Transitioning);
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
        }

        // Attempt to activate immediately / schedule activation again
        ASSERT_EQ(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2), sleeve_status(sleeve_code::setting_already_applied));
        ASSERT_EQ(node->_identity_manager->ChangeActivation(true, 0), sleeve_status(sleeve_code::setting_already_applied));

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Failure scenario: invalid epoch / setting already applied / already scheduled; Cancellation before transition 3==>4";
    }
}

TEST (IdentityManagement, Deactivation)
{
    boost::asio::io_service service;
    std::unique_ptr<logos::thread_runner> runner;

    /// 1. Immediate deactivation between [ETS, ES)
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Immediate deactivation between [ETS, ES)";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Wait till ETS
        std::this_thread::sleep_for(lapse - EPOCH_TRANSITION_START + Milliseconds(10));

        // Immediate deactivation
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(false, 0));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Immediate deactivation between [ETS, ES)";
    }

    /// 2. Sleeve and Activate in 3, schedule for deactivation during epoch transition 3==>4
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve and Activate in 3, schedule for deactivation during epoch transition 3==>4";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);
        auto grace_period = Milliseconds(500);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Wait till right before ETES
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT - grace_period + Milliseconds(10));

        // Schedule for deactivation
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(false, GENESIS_EPOCH + 2));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        std::this_thread::sleep_for(grace_period);

        // Check ETES states (still Persistent despite not activated next)
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);

            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(EPOCH_DELEGATES_CONNECT);
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve and Activate in 3, schedule for deactivation during epoch transition 3==>4";
    }

    /// 3. Sleeve and Activate in 3, schedule for deactivation during epoch transition 4==>5
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Sleeve and Activate in 3, schedule for deactivation during epoch transition 4==>5";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Schedule for deactivation
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(false, GENESIS_EPOCH + 3));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
        ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));

        // Check ETES states
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
            ASSERT_EQ(node->_consensus_container->GetTransitionDelegate(), EpochTransitionDelegate::Persistent);
            ASSERT_EQ(node->_consensus_container->GetTransitionIdx(), 0);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        // Wait till ETES for 4==>5
        std::this_thread::sleep_for(EPOCH_PROPOSAL_TIME);
        {
            auto lock (node->_consensus_container->LockStateAndActivation());
            ASSERT_TRUE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Current));
            ASSERT_FALSE(node->_identity_manager->GetActivationStatus(QueriedEpoch::Next));
            ASSERT_EQ(node->_consensus_container->GetTransitionState(), EpochTransitionState::Connecting);
            ASSERT_TRUE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber())));
            ASSERT_FALSE((bool)(node->_consensus_container->GetEpochManager(node->_consensus_container->GetCurEpochNumber()+1)));
        }

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Sleeve and Activate in 3, schedule for deactivation during epoch transition 4==>5";
    }

    /// 4. Deactivation while Unsleeved
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Deactivation while Unsleeved";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, GENESIS_EPOCH + 2));

        // Cancel activation after ETES should work because we are not Sleeved
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));
        ASSERT_TRUE(node->_identity_manager->CancelActivationScheduling());

        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Deactivation while Unsleeved";
    }
}

TEST (IdentityManagement, CancelScheduling)
{
    /// Cancellation before transition from 3==>4 is tested above

    boost::asio::io_service service;
    std::unique_ptr<logos::thread_runner> runner;

    /// 1. Failure scenario: nothing scheduled
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Failure scenario: nothing scheduled";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        ASSERT_EQ(node->_identity_manager->CancelActivationScheduling(), sleeve_status(sleeve_code::nothing_scheduled));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Failure scenario: nothing scheduled";
    }

    /// 2. Failure scenario: epoch transition already started
    {
        {
            Log log;
            LOG_DEBUG(log) << __func__ << " - STARTED TESTING: Failure scenario: epoch transition already started";
        }
        auto node = PrepNewNode(runner, service, true);
        ASSERT_TRUE(bool(node));
        auto lapse = ArchivalTimer::GetNextEpochTime(true);
        ASSERT_GT(lapse, EPOCH_DELEGATES_CONNECT);

        // Sleeve
        ASSERT_TRUE(node->_identity_manager->Sleeve(bls_prv_str, ecies_prv_str));

        // Activate
        ASSERT_TRUE(node->_identity_manager->ChangeActivation(true, 0));

        // Wait till ETES
        std::this_thread::sleep_for(lapse - EPOCH_DELEGATES_CONNECT + Milliseconds(10));

        // Attempt to cancel activation
        ASSERT_EQ(node->_identity_manager->CancelActivationScheduling(), sleeve_status(sleeve_code::epoch_transition_started));

        std::this_thread::sleep_for(Milliseconds(50));
        LOG_DEBUG(node->_log) << __func__ << " - FINISHED TESTING: Failure scenario: epoch transition already started";
    }
}
