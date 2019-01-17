#include <logos/bootstrap/batch_block_validator.hpp>
#include <logos/bootstrap/p2p.hpp>
#include <logos/consensus/persistence/epoch/nondel_epoch_persistence.hpp>
#include <logos/consensus/persistence/microblock/nondel_microblock_persistence.hpp>
#include <set>

// CTOR
BatchBlock::validator::validator(logos::node *n)
    : node(n),
      nextMicro(0),
      nextEpoch(0)
{
    // Allocate our epoch/micro validators...
    epoch_handler = make_shared<NonDelPersistenceManager<ECT> >(NonDelPersistenceManager<ECT>(node->store));
    micro_handler = make_shared<NonDelPersistenceManager<MBCT> >(NonDelPersistenceManager<MBCT>(node->store));

#ifdef _TIMEOUT_BOOTSTRAP
    std::thread timeout_thr(BatchBlock::validator::timeout_s, this);
    timeout_thr.detach();
    LOG_DEBUG(n->log) << " done BatchBlock::validator init thread " << std::endl;
#endif

    LOG_DEBUG(n->log) << " done BatchBlock::validator " << std::endl;
    nr_blocks = NR_BLOCKS;
    reset(); // Start our counters at 0.
}

BatchBlock::validator::~validator()
{
}

void timeout_s(BatchBlock::validator *this_l)
{
    while(true) {
        sleep(BatchBlock::validator::TIMEOUT);
        this_l->validate(nullptr); // Force us to drain the queue...
    }
}

bool BatchBlock::validator::reset()
{
    nextMicro_counter               = 0;
    nextEpoch_counter               = 0;
    micro_validation_error_counter  = 0;
    epoch_validation_error_counter  = 0;
    micro_not_ready_counter         = 0;
    epoch_not_ready_counter         = 0;

    if(bsb.size() >= nr_blocks) {
        nr_blocks += NR_BLOCKS; // Request more capacity if we did no work.
        LOG_DEBUG(node->log) << "nr_blocks: " << nr_blocks << std::endl;
    }
}

std::map<int, std::pair<int64_t, BlockHash> > BatchBlock::validator::in_memory_bsb_tips()
{
    std::map<int, std::pair<int64_t, BlockHash> > tips;
    std::sort(bsb.begin(), bsb.end(),
        [](const std::shared_ptr<BatchBlock::bulk_pull_response> &lhs,
           const std::shared_ptr<BatchBlock::bulk_pull_response> &rhs)
        {
            return lhs->block.timestamp < rhs->block.timestamp;
        }
    );

    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
       for(int j = bsb.size()-1; j >= 0; --j) {
            if(bsb[j]->delegate_id == i) {
                tips[i] = std::make_pair(bsb[j]->block.sequence,bsb[j]->block.Hash());
                break;
            }
       }
    }

    return tips;
}

std::pair<int64_t, BlockHash> BatchBlock::validator::in_memory_micro_tips()
{
    BlockHash zero = 0;
    if(micro.size() <= 0) {
        return std::make_pair(-1,zero);
    }
    std::sort(micro.begin(), micro.end(),
        [](const std::shared_ptr<BatchBlock::bulk_pull_response_micro> &lhs,
           const std::shared_ptr<BatchBlock::bulk_pull_response_micro> &rhs)
        {
            return lhs->micro.epoch_number <= rhs->micro.epoch_number && lhs->micro.sequence < rhs->micro.sequence;
        }
    );

    return std::make_pair(micro[micro.size()-1]->micro.sequence, micro[micro.size()-1]->micro.Hash());
}

std::pair<int64_t, BlockHash> BatchBlock::validator::in_memory_epoch_tips()
{
    BlockHash zero = 0;
    if(epoch.size() <= 0) {
        return std::make_pair(-1,zero);
    }
    std::sort(epoch.begin(), epoch.end(),
        [](const std::shared_ptr<BatchBlock::bulk_pull_response_epoch> &lhs,
           const std::shared_ptr<BatchBlock::bulk_pull_response_epoch> &rhs)
        {
            return lhs->epoch.epoch_number < rhs->epoch.epoch_number;
        }
    );

    return std::make_pair(epoch[epoch.size()-1]->epoch.epoch_number, epoch[epoch.size()-1]->epoch.Hash());
}

void BatchBlock::validator::add_micro_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m)
{
    std::lock_guard<std::mutex> lock(mutex);
    std::cout << "BatchBlock::validator::add_micro_block:: " << std::endl;
    in_memory_micro_tips();
    std::shared_ptr<BatchBlock::bulk_pull_response_micro> prior_micro = nullptr;

    // Get prior_micro...
    for(int i = micro.size()-1; i >= 0; --i) {
        if(micro[i]->micro.epoch_number <= m->micro.epoch_number &&
           micro[i]->micro.sequence <= m->micro.sequence) {
           //micro[i]->micro.sequence < m->micro.sequence) {
            prior_micro = micro[i];
            break;
        }
    }

//#if 0
    if(prior_micro && (m->micro.Hash() == prior_micro->micro.Hash())) {
        std::cout << "already seen this micro" << std::endl;
        return;
    }
//#endif

    micro.push_back(m);

    auto bsb_tips = in_memory_bsb_tips();
    BlockHash prior_bsb_tip = 0;
    BlockHash zero = 0;

    // Get bsb blocks for this micro block...
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(node->store, i);
        uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(node->store, i);
        auto iter = bsb_tips.find(i);
        if(iter != bsb_tips.end()) {
            bsb_seq = iter->second.first;
            bsb_tip = iter->second.second;
        }
        auto isBSBPresent = BatchBlock::readBatchStateBlock(node->store, m->micro.tips[i]);
        if(prior_micro != nullptr) {
            prior_bsb_tip = prior_micro->micro.tips[i];
        } else if(prior_micro == nullptr && bsb_seq == BatchBlock::NOT_FOUND) {
            prior_bsb_tip = m->micro.tips[i];
        } else {
            prior_bsb_tip = bsb_tip;
        }
        std::cout << "prior_bsb_tip: " << prior_bsb_tip.to_string() << " bsb_seq: " << bsb_seq << " isBSBPresent: " << (isBSBPresent == nullptr) << " bsb_tip: " << bsb_tip.to_string() << " m->tips: " << m->micro.tips[i].to_string() << std::endl;
        if(isBSBPresent == nullptr && bsb_seq == BatchBlock::NOT_FOUND) {
            // Init, we have nothing for this delegate...
            std::cout << "add_pull_bsb: " << __LINE__ << " file: " << __FILE__ << std::endl;
            attempt->add_pull_bsb(
                         0,0,
                         0,0,
                         i,
                         prior_bsb_tip,prior_bsb_tip);
            LOG_DEBUG(node->log) << "logos::BatchBlock::validator::add_micro_block:: init bulk_pull: delegate_id: " << i << " tips: " << m->micro.tips[i].to_string() << std::endl; 
        } else if(isBSBPresent == nullptr && bsb_tip != m->micro.tips[i]) {
            std::cout << "add_pull_bsb: " << __LINE__ << " file: " << __FILE__ << std::endl;
            attempt->add_pull_bsb(
                         0,0,
                         0,0,
                         i,
                         prior_bsb_tip,m->micro.tips[i]);
            LOG_DEBUG(node->log) << "logos::BatchBlock::validator::add_micro_block:: init bulk_pull: delegate_id: " 
                                             << i << " my tip: " << bsb_tip.to_string() << " their tip: " << m->micro.tips[i].to_string() << std::endl; 
         }
    }

    // Check if we are at the last micro block, and get remaining bsb...
    if(tips.size() >= 1) {
        if(m->micro.Hash() == tips[0]->micro_block_tip) {
            for(int i = 0; i < NUMBER_DELEGATES; ++i) {
                BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(node->store, i);
                uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(node->store, i);
                auto iter = bsb_tips.find(i);
                if(iter != bsb_tips.end()) {
                    bsb_seq = iter->second.first;
                    bsb_tip = iter->second.second;
                }
                if(prior_micro) {
                    std::cout << "add_pull_bsb: " << __LINE__ << " file: " << __FILE__ << std::endl;
                    attempt->add_pull_bsb(0,0,0,0,i,prior_micro->micro.tips[i],tips[0]->batch_block_tip[i]);
                } else if(bsb_tip == zero) {
                    std::cout << "add_pull_bsb: " << __LINE__ << " file: " << __FILE__ << std::endl;
                    std::cout << " tips.bsb_tip: " << tips[0]->batch_block_tip[i].to_string() << std::endl;
                    attempt->add_pull_bsb(0,0,0,0,i,tips[0]->batch_block_tip[i],tips[0]->batch_block_tip[i]);

                } else { //if(bsb_tip != tips[0]->batch_block_tip[i]) {
                    std::cout << "add_pull_bsb: " << __LINE__ << " file: " << __FILE__ << std::endl;
                    std::cout << "bsb_tip: " << bsb_tip.to_string() << " tips.bsb_tip: " << tips[0]->batch_block_tip[i].to_string() << std::endl;
                    attempt->add_pull_bsb(0,0,0,0,i,bsb_tip,tips[0]->batch_block_tip[i]); 
                }
            }
        }
    }
}

void BatchBlock::validator::add_epoch_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_epoch> &e)
{
    std::lock_guard<std::mutex> lock(mutex);
    std::cout << "BatchBlock::validator::add_epoch_block:: " << std::endl;
    in_memory_epoch_tips();
    std::shared_ptr<BatchBlock::bulk_pull_response_epoch> prior_epoch = nullptr;
    // Get prior_epoch...
    for(int i = epoch.size()-1; i >= 0; --i) {
        if(epoch[i]->epoch.epoch_number <= e->epoch.epoch_number) {
            prior_epoch= epoch[i];
            break;
        }
    }

    if(prior_epoch && (e->epoch.Hash() == prior_epoch->epoch.Hash())) {
        std::cout << "already seen this epoch" << std::endl;
        return;
    }

    epoch.push_back(e);

    if(prior_epoch && prior_epoch->epoch.Hash() == e->epoch.Hash()) {
        prior_epoch = nullptr; // Avoid self reference.
    }

    BlockHash micro_tip = Micro::getMicroBlockTip(node->store);
    uint32_t  micro_seq = Micro::getMicroBlockSeqNr(node->store);
    auto in_memory_micro_tip = in_memory_micro_tips();

    if(!in_memory_micro_tip.second.is_zero() && in_memory_micro_tip.first >= 0) {
        micro_seq = in_memory_micro_tip.first;
        micro_tip = in_memory_micro_tip.second;
    }

    // Check if we are in-sync
    std::shared_ptr<ApprovedMB> isMicroPresent =  Micro::readMicroBlock(node->store, e->epoch.micro_block_tip);
    if(isMicroPresent == nullptr && micro_tip != e->epoch.micro_block_tip) {
        // Not in sync, get this micro block...
        if(prior_epoch != nullptr) {
            std::cout << "add_pull_micro: " << __LINE__ << " file: " << __FILE__ << std::endl;
            attempt->add_pull_micro(0,0,0,0,prior_epoch->epoch.micro_block_tip,e->epoch.micro_block_tip);
        } else {
            std::cout << "add_pull_micro: " << __LINE__ << " file: " << __FILE__ << std::endl;
            attempt->add_pull_micro(0,0,0,0,micro_tip,e->epoch.micro_block_tip);
        }
    } else {
        LOG_DEBUG(node->log) << "micro block in sync: " << micro_tip.to_string() << std::endl;
    }

    // Check if we are at the last epoch block, and get remaining micros...
    if(tips.size() >= 1) {
        if(e->epoch.Hash() == tips[0]->epoch_block_tip) {
            if(prior_epoch != nullptr) {
                std::cout << "add_pull_micro: " << __LINE__ << " file: " << __FILE__ << std::endl;
                attempt->add_pull_micro(0,0,0,0,prior_epoch->epoch.micro_block_tip,tips[0]->micro_block_tip);
            } else {
                std::cout << "add_pull_micro: " << __LINE__ << " file: " << __FILE__ << std::endl;
                attempt->add_pull_micro(0,0,0,0,micro_tip,tips[0]->micro_block_tip);
            }
        }
    }
}

bool BatchBlock::validator::validate(std::shared_ptr<BatchBlock::bulk_pull_response> block)
{
    std::lock_guard<std::mutex> lock(mutex);
    LOG_DEBUG(node->log) << "validate: bsb.size(): " << bsb.size() << " micro.size(): " << micro.size() << " epoch.size(): " << epoch.size() << std::endl;

    if(block != nullptr) {
        LOG_DEBUG(node->log) << "received bsb: " << block->block.Hash().to_string() <<  " time: " << currentDateTime() << std::endl;
        bsb.push_back(block);
    }

    // Sort all based on timestamp.
    std::sort(bsb.begin(), bsb.end(),
        [](const std::shared_ptr<BatchBlock::bulk_pull_response> &lhs,
           const std::shared_ptr<BatchBlock::bulk_pull_response> &rhs)
        {
            return lhs->block.timestamp < rhs->block.timestamp;
        }
    );

   std::set<int> finished;
   for(int i = 0; i < bsb.size(); ++i) {
        std::shared_ptr<BatchBlock::bulk_pull_response> block = bsb[i];
        LOG_DEBUG(node->log) << "trying to validate: " << std::endl;
        if (BatchBlock::Validate(node->store,
                block->block,block->delegate_id)) {
                // Block is valid, add to database.
                BatchBlock::ApplyUpdates(node->store,
                                 block->block,block->delegate_id);
                std::cout << "validate successful: hash: " << block->block.Hash().to_string() << " prev: " << block->block.previous.to_string() << " next: " << block->block.next.to_string() << " delegate_id: " << block->delegate_id << std::endl;
                LOG_INFO(node->log) << "validate successful: hash: " << block->block.Hash().to_string() << " prev: " << block->block.previous.to_string() << " next: " << block->block.next.to_string() << " delegate_id: " << block->delegate_id << std::endl;
                finished.insert(i);
        } else {
                block->retry_count++; // How many times this block failed...
                if(block->retry_count >= BatchBlock::validator::MAX_BSB_RETRY) {
                    // Remove block and blacklist...
                    finished.insert(i);
                    p2p::add_to_blacklist(block->peer); // TODO Should we also blacklist all our peers up and to including this one?
                }
                LOG_DEBUG(node->log) << "validate failed: hash: " << block->block.Hash().to_string() << " prev: " << block->block.previous.to_string() << " next: " << block->block.next.to_string() << " delegate_id: " << block->delegate_id << std::endl;
        }
   }

   // Clean-up the vector removing blocks we processed.
   std::vector<std::shared_ptr<bulk_pull_response> > tmp;
   for(int i = 0; i < bsb.size(); ++i) {
        if(finished.end() == finished.find(i)) {
            tmp.push_back(bsb[i]);
        }
   }

   // Update the vector with the bsb blocks we processed.
   bsb = tmp;

   bool ready = true;

   in_memory_micro_tips();

   LOG_DEBUG(node->log) << "remaining: {" << std::endl;
   for(int i = 0; i < micro.size(); ++i) {
        LOG_DEBUG(node->log) << "remaining: " << micro[i]->micro.Hash().to_string() << " prev: " << micro[i]->micro.previous.to_string() << " next: " << micro[i]->micro.next.to_string() << std::endl;
   }
   LOG_DEBUG(node->log) << "remaining: }" << std::endl;

   for(int j = 0; j < micro.size(); ++j) {
      ::ApprovedMB *peerMicro = &micro[j]->micro;

      BlockHash peerHash = peerMicro->Hash();
      std::shared_ptr<ApprovedMB> isMicroPresent =  Micro::readMicroBlock(node->store, peerHash);
      if(ready && isMicroPresent == nullptr) {
        //using Request = RequestMessage<MBCT>;
		ValidationStatus rtvl;
		//TODO
        if((!peerMicro->previous.is_zero() || !peerMicro->next.is_zero()) && micro_handler->Validate(*peerMicro, &rtvl)) {
             std::cout << "micro_handler->Validate: " 
                      << peerMicro->Hash().to_string() << " prev: " << peerMicro->previous.to_string()
                      << " next: " << peerMicro->next.to_string() << std::endl;
             LOG_DEBUG(node->log) << "micro_handler->Validate: " 
                      << peerMicro->Hash().to_string() << " prev: " << peerMicro->previous.to_string()
                      << " next: " << peerMicro->next.to_string() << std::endl;
             micro_handler->ApplyUpdates(*peerMicro); // Validation succeeded, add to database.
        } else {
             // Try several times, if fail, we assume there is a problem.
             LOG_DEBUG(node->log) 
                      << "error validating: " << peerMicro->Hash().to_string() << " prev: " << peerMicro->previous.to_string()
                      << " next: " << peerMicro->next.to_string() << std::endl;
        }
      }
   }

   in_memory_epoch_tips();

   LOG_DEBUG(node->log) << "remaining epoch: {" << std::endl;
   for(int i = 0; i < epoch.size(); ++i) {
        LOG_DEBUG(node->log) << "remaining epoch: " << epoch[i]->epoch.Hash().to_string() << " prev: " << epoch[i]->epoch.previous.to_string() << " next: " << epoch[i]->epoch.next.to_string() << std::endl;
    }
   LOG_DEBUG(node->log) << "remaining epoch: }" << std::endl;

   BlockHash current_micro_hash = Micro::getMicroBlockTip(node->store);
   bool isValid = false;
   for(int j = 0; j < epoch.size(); ++j) {
        //using Request = RequestMessage<ECT>;
        //logos::process_return rtvl;

        ValidationStatus rtvl;
        //TODO
        if(epoch[j]->epoch.micro_block_tip == current_micro_hash && 
           (isValid=epoch_handler->Validate(epoch[j]->epoch, &rtvl))) {
            std::cout << "epoch_handler->ApplyUpdates: " << epoch[j]->epoch.Hash().to_string() << std::endl;
            LOG_INFO(node->log) << "epoch_handler->ApplyUpdates: " << epoch[j]->epoch.Hash().to_string() << std::endl;
            epoch_handler->ApplyUpdates(epoch[j]->epoch); // Validation succeeded, add to database.
       } else {
            LOG_DEBUG(node->log) << "epoch_handler->Failed Validation: " << epoch[j]->epoch.micro_block_tip.to_string() << " current: " << current_micro_hash.to_string() << " isValid: " << isValid << std::endl;
       }
   }

   return false; // Ok, no error...
}
