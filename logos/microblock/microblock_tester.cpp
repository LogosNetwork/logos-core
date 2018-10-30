#include <logos/microblock/microblock_handler.hpp>
#include <logos/microblock/microblock_tester.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/epoch_time_util.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/blocks.hpp>

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
  logos::state_block state_block;
  Signature signature;
  static logos::block_hash previous[32]{0}; // should be current epoch for 1st block
  for (uint8_t i_del = 0; i_del < ndelegates; ++i_del) {
    for (int i_batch = 0; i_batch < n_batch_blocks; ++i_batch) {
      BatchStateBlock batch_block;
      batch_block.block_count = 100;
      batch_block.signature = signature;
      batch_block.timestamp = GetStamp(); // need to model block spread
      for (int i_state = 0; i_state < n_state_blocks; ++i_state) {
        //batch_block.blocks[i_state].signature = signature;
        logos::account account(rand());
        logos::block_hash hash(rand());
        batch_block.blocks[i_state].hashables.account = account;
        batch_block.blocks[i_state].hashables.previous = hash;
        batch_block.blocks[i_state].hashables.representative = account;
        batch_block.blocks[i_state].hashables.amount = 1000;
        batch_block.blocks[i_state].hashables.link = logos::block_hash(0);
      }
      batch_block.previous = previous[i_del];
      previous[i_del] = batch_block.Hash();
      node.store.batch_block_put(batch_block, transaction);
      node.store.batch_tip_put(i_del, previous[i_del], transaction);
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
  uint64_t work = 0;

  logos::state_block state(pair.pub,  // account
                           0,         // previous
                           pair.pub,  // representative
                           amount,
                           fee,
                           pair.pub,  // link
                           pair.prv,
                           pair.pub,
                           work);
    std::string contents;
    state.serialize_json(contents);
    boost::log::sources::logger_mt log;
    BOOST_LOG(log) << "initializing delegate " <<
                                    pair.prv.data.to_string() << " " <<
                                    pair.pub.to_string() << " " <<
                                    pair.pub.to_account() << " " <<
                                    state.hash().to_string() << "\n" << contents;

  node.store.receive_put(state.hash(),
                      state,
                      transaction);

  node.store.account_put(pair.pub,
                      {
                              /* Head    */ 0,
                              /* Previous*/ 0,
                              /* Rep     */ 0,
                              /* Open    */ state.hash(),
                              /* Amount  */ amount,
                              /* Time    */ logos::seconds_since_epoch(),
                              /* Count   */ 0
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
  for (auto it = logos::store_iterator(transaction, node.store.account_db); it != logos::store_iterator(nullptr); ++it) {
    logos::account account(it->first.uint256());
    logos::account_info info(it->second);
    boost::property_tree::ptree response;
    response.put ("frontier", info.head.to_string ());
    response.put ("open_block", info.open_block.to_string ());
    response.put ("representative_block", info.rep_block.to_string ());
    std::string balance;
    logos::uint128_union (info.balance).encode_dec (balance);
    response.put ("balance", balance);
    response.put ("modified_timestamp", std::to_string (info.modified));
    response.put ("block_count", std::to_string (info.block_count));
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
  node._archiver.Test_ProposeMicroBlock(node._consensus_container, last_block);
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
        node._consensus_container.OnSendRequest(epoch);
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
    EventProposer proposer(node.alarm, false);
    proposer.ProposeTransitionOnce([&node]()->void{
        node._consensus_container.EpochTransitionEventsStart();
    });
    response_l.put ("result", "in-progress");
    response (response_l);
}

void
MicroBlockTester::informational(
    std::function<void(boost::property_tree::ptree const &)> response,
    logos::node &node)
{
    boost::property_tree::ptree response_l;

    Epoch epoch;
    size_t s = sizeof(epoch);
    std::stringstream str;
    uint64_t start = (uint64_t)&epoch;
    uint64_t account = (uint64_t)&(epoch.account);
    uint64_t enumber = (uint64_t)&(epoch.epoch_number);
    uint64_t tip = (uint64_t)&(epoch.micro_block_tip);
    uint64_t del = (uint64_t)&(epoch.delegates);
    uint64_t fee = (uint64_t)&(epoch.transaction_fee_pool);
    uint64_t sig = (uint64_t)&(epoch.signature);
    str <<  "epoch offsets: account " << (account - start) << " enumber " << (enumber-start)
        << " tip " << (tip-start) << " delegates " << (del-start) << " fee " << (fee-start)
        << " sig " << (sig-start) << " size " << sizeof(epoch);
    response_l.put ("result", str.str());

    response (response_l);
}

void
MicroBlockTester::epoch_delegates(
    std::function<void(boost::property_tree::ptree const &)> response,
    logos::node &node)
{
    using Accounts = logos::account[NUM_DELEGATES];
    boost::property_tree::ptree response_l;
    uint8_t delegate_idx;
    Accounts delegates;

    std::string epoch (_request.get<std::string> ("epoch", "current"));

    if (epoch == "current")
    {
        node._identity_manager.IdentifyDelegates(EpochDelegates::Current, delegate_idx, delegates);
    }
    else
    {
        node._identity_manager.IdentifyDelegates(EpochDelegates::Next, delegate_idx, delegates);
    }

    int del = 0;
    for (auto acct : delegates) {
        boost::property_tree::ptree response;
        char buff[5];
        sprintf(buff, "%d", del);
        response.put("ip", NodeIdentityManager::_delegates_ip[acct]);
        response_l.push_back(std::make_pair(buff, response));
        del++;
    }

    response (response_l);
}
