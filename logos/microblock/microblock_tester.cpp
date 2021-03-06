#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/microblock/microblock_tester.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/epoch_time_util.hpp>
#include <logos/node/utility.hpp>
#include <logos/blockstore.hpp>

boost::property_tree::ptree MicroBlockTester::_request;


bool
MicroBlockTester::microblock_tester(
        std::string &action,
        boost::property_tree::ptree request,
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    _request = request;
    bool res = true;
    if (action == "block_create_test")
    {
        block_create_test(response, node);
    }
    else if (action == "precreate_account")
    {
        precreate_account(response, node);
    }
    else if (action == "read_accounts")
    {
        read_accounts(response, node);
    }
    else if (action == "generate_microblock")
    {
        generate_microblock(response, node);
    }
    else if (action == "generate_epoch")
    {
        generate_epoch(response, node);
    }
    else if (action == "disable_test")
    {
        boost::property_tree::ptree response_l;
        response_l.put ("result", "disabled");
        response (response_l);
    }
    else if (action == "start_epoch_transition")
    {
        start_epoch_transition(response, node);
    }
    else if (action == "informational")
    {
        informational(response, node);
    }
    else if (action == "epoch_delegates")
    {
        epoch_delegates(response, node);
    }
    else if (action == "advertise")
    {
        advertise(response, node);
    }
    else
    {
        res = false;
    }
    return res;
}

void
MicroBlockTester::block_create_test(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    logos::transaction transaction(node.store.environment, nullptr, true);
    boost::property_tree::ptree response_l;
    response_l.put ("result", "created blocks");
    // each call create fake single state and batch blocks for testing
    // 10 state blocks per batch block, 32 chains of batch blocks for each
    // delegate, 100 batch blocks
    // create delegates if needed
    int ndelegates = 32;
    int n_batch_blocks = 100; // need to randomize to simulate different arrival
    int n_state_blocks = 100;
    DelegateSig delegate_sig;
    AccountSig account_sig;
    static BlockHash previous[32]; // should be current epoch for 1st block

    for (uint8_t i_del = 0; i_del < ndelegates; ++i_del) {
        for (int i_batch = 0; i_batch < n_batch_blocks; ++i_batch) {
            ApprovedRB batch_block;
            for(size_t i = 0; i < 100; ++i)
            {
                batch_block.requests.push_back(std::shared_ptr<Send>(new Send()));
            }
            batch_block.preprepare_sig = delegate_sig;
            batch_block.timestamp = GetStamp(); // need to model block spread
            for (int i_state = 0; i_state < n_state_blocks; ++i_state) {
                auto request = static_pointer_cast<Send>(batch_block.requests[i_state]);
                request->signature = account_sig;
                logos::account account(rand());
                BlockHash hash(rand());
                request->origin = account;
                request->previous = hash;
                request->AddTransaction(AccountAddress(), 1000);
            }
            batch_block.previous = previous[i_del];
            previous[i_del] = batch_block.Hash();
            node.store.request_block_put(batch_block, transaction);
            node.store.request_tip_put(i_del, batch_block.epoch_number, batch_block.CreateTip(), transaction);
        }
    }
    response (response_l);
}

void
MicroBlockTester::precreate_account(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    logos::transaction transaction(node.store.environment, nullptr, true);
    boost::property_tree::ptree response_l;
    logos::keypair pair;

    logos::amount amount(std::numeric_limits<logos::uint128_t>::max());
    logos::amount fee(0);

    AccountAddress account = pair.pub;
    AccountPubKey pub_key = pair.pub;
    AccountPrivKey priv_key = pair.prv.data;

    Send request(account,     // account
                 BlockHash(), // previous
                 0,           // sqn
                 account,     // link
                 amount,
                 fee,
                 priv_key,
                 pub_key);

    std::string contents = request.ToJson();
    boost::log::sources::logger_mt log;
    LOG_DEBUG(log) << "initializing delegate "
                   << pair.prv.data.to_string() << " "
                   << pair.pub.to_string() << " "
                   // <<  pair.pub.to_account() << " "
                   << request.GetHash().to_string() << "\n"
                   << contents;

    ReceiveBlock receive(0, request.GetHash(), 0);
    node.store.receive_put(request.GetHash(), receive, transaction);

    node.store.account_put(account,
                      {
                              /* Head        */ 0,
                              /* Previous    */ 0,
                              /* Rep         */ 0,
                              /* Open        */ request.GetHash(),
                              /* Amount      */ amount,
                              /* Time        */ logos::seconds_since_epoch(),
                              /* Count       */ 0,
                              /* Receive     */ 0,
                              /* Claim Epoch */ 0
                      },
                      transaction);

    response_l.put("private", pair.prv.data.to_string());
    response_l.put("public", pair.pub.to_string());
    response_l.put("account", pair.pub.to_account());

    response (response_l);
}

void
MicroBlockTester::read_accounts(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    boost::property_tree::ptree response_l;
    logos::transaction transaction(node.store.environment, nullptr, false);
    int i = 0;
    for (auto it = logos::store_iterator(transaction, node.store.account_db);
         it != logos::store_iterator(nullptr); ++it)
    {
        logos::account account(it->first.uint256());
        bool error = false;
        logos::account_info info(error, it->second);
        boost::property_tree::ptree response;
        response.put ("frontier", info.head.to_string ());
        response.put ("open_block", info.open_block.to_string ());
        response.put ("representative_block", info.governance_subchain_head.to_string ());
        std::string balance;
        logos::uint128_union (info.GetBalance()).encode_dec (balance);
        response.put ("balance", balance);
        response.put ("modified_timestamp", std::to_string (info.modified));
        response.put ("request_count", std::to_string (info.block_count));
        response_l.push_back (std::make_pair (account.to_account (), response));
    }
    response (response_l);
}

void
MicroBlockTester::generate_microblock(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    logos::transaction transaction(node.store.environment, nullptr, true);
    boost::property_tree::ptree response_l;
    bool last_block = _request.get<bool>("last", false);
    node._consensus_container->_archiver.Test_ProposeMicroBlock(*node._consensus_container, last_block);
    response_l.put ("result", "sent");
    response (response_l);
}

void
MicroBlockTester::generate_epoch(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    boost::property_tree::ptree response_l;
    /*logos::transaction transaction(node.store.environment, nullptr, true);
    EventProposer proposer(node.alarm);
    proposer.ProposeTransitionOnce([&node]()->void{
        EpochHandler handler(node.store);
        auto epoch = std::make_shared<Epoch>();
        node._consensus_container.OnDelegateMessage(epoch);
    }, std::chrono::seconds(1)); */
    response_l.put ("result", "not-implemented");
    response (response_l);
}

void
MicroBlockTester::start_epoch_transition(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    boost::property_tree::ptree response_l;
    int delay = _request.get<int>("delay", 0);
    RecallHandler handler;
    EventProposer proposer(node.alarm, handler);
    proposer.ProposeTransitionOnce([&node]()->void{
        node._consensus_container->EpochTransitionEventsStart();
    }, Seconds(delay));
    response_l.put ("result", "in-progress");
    response (response_l);
}

void
MicroBlockTester::informational(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    boost::property_tree::ptree response_l;
    std::stringstream str;

    std::string type = _request.get<std::string>("type", "");

    if (type == "epoch")
    {
        ApprovedEB epoch;
        size_t size = 0;

        ECIESPublicKey ecies;
        ecies.FromHexString("8e1ad798008baac3663c0c1a6ce04c7cb632eb504562de923845fccf39d1c46dee52df70f6cf46f1351ce7ac8e92055e5f168f5aff24bcaab7513d447fd677d3");

        std::vector<uint8_t> buf;
        {
            logos::vectorstream stream(buf);
            size = ecies.Serialize(stream);
        }

        uint64_t start = (uint64_t) &epoch;
        uint64_t account = (uint64_t) &(epoch.primary_delegate);
        uint64_t enumber = (uint64_t) &(epoch.epoch_number);
        uint64_t tip = (uint64_t) &(epoch.micro_block_tip);
        uint64_t fee = (uint64_t) &(epoch.transaction_fee_pool);
        uint64_t del = (uint64_t) &(epoch.delegates);
        uint64_t next = (uint64_t) &(epoch.next);
        uint64_t sig = (uint64_t) &(epoch.preprepare_sig);
        str << "epoch offsets: account " << (account - start) << " enumber " << (enumber - start)
            << " tip " << (tip - start) << " fee " << (fee - start) << " delegates " << (del - start)
            << " next " << (next - start) << " sig " << (sig - start) << " size " << sizeof(epoch)
            << " ecies size " << size;
    }
    else if (type == "microblock")
    {
        ApprovedMB block;

        uint64_t start = (uint64_t) &block;
        uint64_t account = (uint64_t) &(block.primary_delegate);
        uint64_t enumber = (uint64_t) &(block.epoch_number);
        uint64_t seq = (uint64_t) &(block.sequence);
        uint64_t last = (uint64_t) &(block.last_micro_block);
        uint64_t num = (uint64_t) &(block.number_batch_blocks);
        uint64_t tips = (uint64_t) &(block.tips);
        uint64_t sig = (uint64_t) &(block.preprepare_sig);
        str << "epoch offsets: account " << (account - start) << " enumber " << (enumber - start)
            << " sequence " << (seq - start) << " last " << (last - start)
            << "num blocks " << (num - start) << " tips " << (tips - start)
            << " sig " << (sig - start) << " size " << sizeof(block);
    }
    else if (type == "batch")
    {
        ApprovedRB block;
        uint64_t start = (uint64_t) &block;
        uint64_t account = (uint64_t) &(block.primary_delegate);
        uint64_t seq = (uint64_t) &(block.sequence);
        uint64_t enumber = (uint64_t) &(block.epoch_number);
        uint64_t blocks = (uint64_t) &(block.requests);
        uint64_t next = (uint64_t) &(block.next);
        uint64_t sig = (uint64_t) &(block.preprepare_sig);
        str << "batch offsets: account " << (account - start) << " sequence " << (seq - start)
            << " epoch " << (enumber - start) << " blocks " << (blocks - start)
            << " next " << (next - start) << " sig " << (sig - start) << " size " << sizeof(block);
    }
    else if (type == "account")
    {
        logos::account_info info;
        uint64_t start = (uint64_t)&info;
        uint64_t typ = (uint64_t)&info.type;
        uint64_t balance = (uint64_t)&(info.GetBalance());
        uint64_t modified = (uint64_t)&info.modified;
        uint64_t head = (uint64_t)&info.head;
        uint64_t block_count = (uint64_t)&info.block_count;
        uint64_t receive_head = (uint64_t)&info.receive_head;
        uint64_t receive_count = (uint64_t)&info.receive_count;
        uint64_t rep_block = (uint64_t)&info.governance_subchain_head;
        uint64_t open_block = (uint64_t)&info.open_block;
        uint64_t entries = (uint64_t)&info.entries;
        std::vector<uint8_t> buf;
        size_t size;
        {
            logos::vectorstream stream(buf);
            size = info.Serialize(stream);
        }

        str << "account offsets: type " << (typ-start) << " balance " << (balance-start)
            << " modified " << (modified-start) << " head " << (head-start)
            << " block count " << (block_count-start) << " receive head " << (receive_head-start)
            << " receive count " << (receive_count-start) << " rep block" << (rep_block-start)
            << " open block " << (open_block-start) << " entries " << (entries-start)
            << " size " << size;
    }
    response_l.put("result", str.str());

    response (response_l);
}

void
MicroBlockTester::epoch_delegates(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    using Accounts = AccountAddress[NUM_DELEGATES];
    boost::property_tree::ptree response_l;
    uint8_t delegate_idx;
    std::shared_ptr<ApprovedEB> approvedEb;

    std::string epoch (_request.get<std::string> ("epoch", "current"));
    if (epoch == "current")
    {
        node._identity_manager->IdentifyDelegates(QueriedEpoch::Current, delegate_idx, approvedEb);
    }
    else
    {
        node._identity_manager->IdentifyDelegates(QueriedEpoch::Next, delegate_idx, approvedEb);
    }

    for (uint8_t del = 0; del < NUM_DELEGATES; del++)
    {
        boost::property_tree::ptree response;
        char buff[5];
        sprintf(buff, "%d", (int)del);
        response.put("ip", node._identity_manager->GetDelegateIP(approvedEb->epoch_number+2, del));
        response_l.push_back(std::make_pair(buff, response));
    }

    response (response_l);
}

void
MicroBlockTester::advertise(
        std::function<void(boost::property_tree::ptree const &)> response,
        logos::node &node)
{
    boost::property_tree::ptree response_l;

    uint8_t idx;
    std::shared_ptr<ApprovedEB> eb;
    node._identity_manager->CheckAdvertise(ConsensusContainer::GetCurEpochNumber(), true, idx, eb);

    response_l.put("result", "processing");

    response(response_l);
}
