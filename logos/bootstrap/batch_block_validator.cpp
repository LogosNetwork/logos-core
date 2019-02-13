#include <logos/bootstrap/batch_block_validator.hpp>
#include <logos/bootstrap/p2p.hpp>
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

    LOG_DEBUG(n->log) << " done BatchBlock::validator " << std::endl;
    nr_blocks = NR_BLOCKS;
    reset(); // Start our counters at 0.

    std::thread timeout_thr(BatchBlock::validator::timeout_s, this);
    timeout_thr.detach();
    LOG_DEBUG(n->log) << " done BatchBlock::validator init thread " << std::endl;
}

void BatchBlock::validator::timeout_s(BatchBlock::validator *this_l)
{
    while(true) {
        sleep(BatchBlock::validator::TIMEOUT);
        this_l->validate(nullptr, nullptr); // Force us to drain the queue...
    }
}

BatchBlock::validator::~validator()
{
}

bool BatchBlock::validator::reset()
{
    nextMicro_counter               = 0;
    nextEpoch_counter               = 0;
    micro_validation_error_counter  = 0;
    epoch_validation_error_counter  = 0;
    micro_not_ready_counter         = 0;
    epoch_not_ready_counter         = 0;

#if 0
    if(bsb.size() >= nr_blocks) {
        nr_blocks += NR_BLOCKS; // Request more capacity if we did no work.
        LOG_DEBUG(node->log) << "nr_blocks: " << nr_blocks << std::endl;
    }
#endif
}

std::map<int, std::pair<int64_t, BlockHash> > BatchBlock::validator::in_memory_bsb_tips()
{
    std::map<int, std::pair<int64_t, BlockHash> > tips;
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        std::sort(bsb[i].begin(), bsb[i].end(),
            [](const std::shared_ptr<BatchBlock::bulk_pull_response> &lhs,
               const std::shared_ptr<BatchBlock::bulk_pull_response> &rhs)
            {
                //return lhs->block->timestamp < rhs->block->timestamp;
                return lhs->block->sequence < rhs->block->sequence;
            }
        );
    }

    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
       for(int j = bsb[i].size()-1; j >= 0; --j) {
            if(bsb[i][j]->delegate_id == i) {
                tips[i] = std::make_pair(bsb[i][j]->block->sequence,bsb[i][j]->block->Hash());
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
            return lhs->micro->epoch_number <= rhs->micro->epoch_number && lhs->micro->sequence < rhs->micro->sequence;
        }
    );

    return std::make_pair(micro[micro.size()-1]->micro->sequence, micro[micro.size()-1]->micro->Hash());
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
            return lhs->epoch->epoch_number < rhs->epoch->epoch_number;
        }
    );

    return std::make_pair(epoch[epoch.size()-1]->epoch->epoch_number, epoch[epoch.size()-1]->epoch->Hash());
}

void BatchBlock::validator::add_micro_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m)
{
    std::lock_guard<std::mutex> lock(mutex);
    LOG_DEBUG(node->log) << "BatchBlock::validator::add_micro_block:: " << m->micro->Hash().to_string() << std::endl;
    in_memory_micro_tips();
    std::shared_ptr<BatchBlock::bulk_pull_response_micro> prior_micro = nullptr;

    // Get prior_micro...
    for(int i = micro.size()-1; i >= 0; --i) {
        if(micro[i]->micro->epoch_number <= m->micro->epoch_number &&
           micro[i]->micro->sequence <= m->micro->sequence) {
           //micro[i]->micro.sequence < m->micro.sequence) {
            prior_micro = micro[i];
            break;
        }
    }

//#if 0
    bool isPrior = false;
    if(prior_micro && (m->micro->Hash() == prior_micro->micro->Hash())) {
        isPrior = true;
        //return;
    }
//#endif

    // Check we are not already in our database, if
    // we are already there, don't queue this micro up...
    auto next_micro = m->micro->Hash();
    auto isMicroPresent = Micro::readMicroBlock(node->store, next_micro);
    if(isMicroPresent || isPrior) {
        LOG_DEBUG(node->log) << " micro block already installed, not queing up, will check our bsb tips..." << std::endl;
    } else {
        // Queue it up for later processing...
        micro.push_back(m);
    }
    auto bsb_tips = in_memory_bsb_tips();
    BlockHash prior_bsb_tip = 0;
    BlockHash zero = 0;

    // Get bsb blocks for this micro block...
    int count = 0;
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        if(m->micro->tips[i].is_zero()) {
            ++count;
            continue; // Skip this...
        }
        BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(node->store, i);
        uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(node->store, i);
        auto iter = bsb_tips.find(i);
#if 0
        if(iter != bsb_tips.end()) { // RGD1 probably want whats on disk...
            bsb_seq = iter->second.first;
            bsb_tip = iter->second.second;
        }
#endif
        auto isBSBPresent = BatchBlock::readBatchStateBlock(node->store, m->micro->tips[i]);
        if(prior_micro != nullptr) {
            prior_bsb_tip = prior_micro->micro->tips[i];
        } else if(prior_micro == nullptr && bsb_seq == BatchBlock::NOT_FOUND) {
            prior_bsb_tip = m->micro->tips[i];
        } else {
            prior_bsb_tip = bsb_tip;
        }
        if(isBSBPresent == nullptr && bsb_seq == BatchBlock::NOT_FOUND) {
            // Init, we have nothing yet for this delegate...
            attempt->add_pull_bsb(
                         0,0,
                         0,0,
                         i,
                         prior_bsb_tip,prior_bsb_tip);
            LOG_DEBUG(node->log) << "logos::BatchBlock::validator::add_micro_block:: init bulk_pull: delegate_id: " << i << " tips: " << m->micro->tips[i].to_string() << std::endl; 
        } else if(isBSBPresent == nullptr && bsb_tip != m->micro->tips[i]) {
            attempt->add_pull_bsb(
                         0,0,
                         0,0,
                         i,
                         prior_bsb_tip,m->micro->tips[i]);
            LOG_DEBUG(node->log) << "logos::BatchBlock::validator::add_micro_block:: init bulk_pull: delegate_id: " 
                                             << i << " my tip: " << bsb_tip.to_string() << " their tip: " << m->micro->tips[i].to_string() << std::endl; 
         }
    }

    if(count == NUMBER_DELEGATES) {
        for(auto i = micro.begin(); i != micro.end(); ++i) {
            if((*i)->micro->Hash() == next_micro) {
                micro.erase(i);
                break;
            }
        }
        return; // The micro block has all zero bsb tips...
    }

    // Check if we are at the last micro block, and get remaining bsb...
    if(tips.size() >= 1) {
        if(m->micro->Hash() == tips[0]->micro_block_tip) {
            for(int i = 0; i < NUMBER_DELEGATES; ++i) {
                BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(node->store, i);
                uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(node->store, i);
                auto iter = bsb_tips.find(i);
                if(iter != bsb_tips.end()) {
                    bsb_seq = iter->second.first;
                    bsb_tip = iter->second.second;
                }
                if(prior_micro) {
                    attempt->add_pull_bsb(0,0,0,0,i,prior_micro->micro->tips[i],tips[0]->batch_block_tip[i]);
                } else if(bsb_tip == zero) {
                    attempt->add_pull_bsb(0,0,0,0,i,tips[0]->batch_block_tip[i],tips[0]->batch_block_tip[i]);

                } else { //if(bsb_tip != tips[0]->batch_block_tip[i]) {
                    attempt->add_pull_bsb(0,0,0,0,i,bsb_tip,tips[0]->batch_block_tip[i]); 
                }
            }
        }
    }
}

void BatchBlock::validator::request_micro_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m)
{
    static std::mutex mymutex;
    std::lock_guard<std::mutex> lock(mymutex);
    BlockHash prior_bsb_tip = 0;
    BlockHash zero = 0;

    // Get bsb blocks for this micro block...
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(node->store, i);
        uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(node->store, i);
        auto isBSBPresent = BatchBlock::readBatchStateBlock(node->store, m->micro->tips[i]);
        prior_bsb_tip = bsb_tip;
            attempt->add_pull_bsb(
                         0,0,
                         0,0,
                         i,
                         prior_bsb_tip,m->micro->tips[i]);
    }
}

void BatchBlock::validator::add_epoch_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_epoch> &e)
{
    std::lock_guard<std::mutex> lock(mutex);
    LOG_DEBUG(node->log) << "BatchBlock::validator::add_epoch_block:: " << e->epoch->Hash().to_string() << std::endl;

    epoch.push_back(e); // logical

}

static
void get_block_sequence_range(std::shared_ptr<ApprovedBSB> block, int *lmin, int *lmax)
{
    *lmin = (1<<20);
    *lmax = 0;

    for(int i = 0; i < block->block_count; i++) {
        if(block->blocks[i]->sequence < *lmin) {
            *lmin = block->blocks[i]->sequence;
        }       
    }

    for(int i = 0; i < block->block_count; i++) {
        if(block->blocks[i]->sequence > *lmax) {
            *lmax = block->blocks[i]->sequence;
        }       
    }
}

bool BatchBlock::validator::validate(std::shared_ptr<logos::bootstrap_attempt> attempt, std::shared_ptr<BatchBlock::bulk_pull_response> block)
{
    std::lock_guard<std::mutex> lock(mutex);
    LOG_DEBUG(node->log) << "validate: bsb.size(): " << bsb[block->delegate_id].size() << " micro.size(): " << micro.size() << " epoch.size(): " << epoch.size() << std::endl;

    if(block != nullptr) {
        LOG_DEBUG(node->log) << "received bsb: " << block->block->Hash().to_string() <<  " time: " << currentDateTime() << std::endl;
        bsb[block->delegate_id].push_back(block);
    }

#if 0
    // Sort all based on timestamp.
    std::sort(bsb.begin(), bsb.end(),
        [](const std::shared_ptr<BatchBlock::bulk_pull_response> &lhs,
           const std::shared_ptr<BatchBlock::bulk_pull_response> &rhs)
        {
            //return lhs->block->timestamp < rhs->block->timestamp;
            //return lhs->block->sequence < rhs->block->sequence;
            int lmin,lmax,rmin,rmax;
            get_block_sequence_range(lhs->block,&lmin,&lmax);
            get_block_sequence_range(rhs->block,&rmin,&rmax);
            //std::cout << "lmin: " << lmin << " rmin: " << rmin << " lmax: " << lmax << " rmax: " << rmax << std::endl;
            //return lmin < rmin && lmax < rmax;
            return lmin < rmin;
        }
    );
#endif
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        std::sort(bsb[i].begin(), bsb[i].end(),
            [](const std::shared_ptr<BatchBlock::bulk_pull_response> &lhs,
               const std::shared_ptr<BatchBlock::bulk_pull_response> &rhs)
            {
                //return lhs->block->timestamp < rhs->block->timestamp;
                return lhs->block->sequence < rhs->block->sequence;
            }
        );
    }

   std::set<int> finished_bsb[NUMBER_DELEGATES];
   std::set<int> finished;

   int fail_count = 0;
   int c_c_fail = 0;
   while(c_c_fail < NUMBER_DELEGATES) {
       for(int j = 0; j < NUMBER_DELEGATES; ++j) {
         if(bsb[j].empty()) {
            ++c_c_fail;
            continue;
         }
	     for(int i = 0; i < bsb[j].size(); ++i) {
	        std::shared_ptr<BatchBlock::bulk_pull_response> block = bsb[j][i];
	        LOG_DEBUG(node->log) << "trying to validate: " << std::endl;
			ValidationStatus rtvl;
	        BlockHash peerHash = block->block->Hash();
	        //std::shared_ptr<ApprovedBSB> isBSBPresent =  BatchBlock::readBatchStateBlock(node->store, peerHash);
	        if(node->store.batch_block_exists(*block->block.get())) {
	            // Remove it from queue if its already present...
	            finished_bsb[j].insert(i);
	        } else {
	            // Try and validate...
	            if (BatchBlock::Validate(node->store,
	                *block->block.get(),block->delegate_id,&rtvl)) {
	                // Block is valid, add to database.
	                BatchBlock::ApplyUpdates(node->store,
	                                 *block->block.get(),block->delegate_id);
	                LOG_INFO(node->log) << "validate successful: hash: " << block->block->Hash().to_string() << " prev: " << block->block->previous.to_string() << " next: " << block->block->next.to_string() << " delegate_id: " << block->delegate_id << std::endl;
	                finished_bsb[j].insert(i);
	            } else {
#if 0
	                bool found = false;
	                std::shared_ptr<BatchBlock::bulk_pull_response> next;
	                int j = 0;
	                for(j = 0; j < bsb.size(); ++j) {
	                    int lmin, lmax;
	                    get_block_sequence_range(bsb[j]->block,&lmin,&lmax);
	                    if(lmin == EXPECTING) {
	                        found = true;
	                        next = bsb[j];
	                        std::cout << "EXPECTED found:" << std::endl;
	                        break;
	                    }
	                }
	                if(found) {
	                    if (BatchBlock::Validate(node->store,
	                        *next->block.get(),next->delegate_id,&rtvl)) {
	                        // Block is valid, add to database.
	                        BatchBlock::ApplyUpdates(node->store,
	                                 *block->block.get(),block->delegate_id);
	                        std::cout << "validate successful: hash: " << block->block->Hash().to_string() << " prev: " << block->block->previous.to_string() << " next: " << block->block->next.to_string() << " delegate_id: " << block->delegate_id << std::endl;
	                        LOG_INFO(node->log) << "validate successful: hash: " << block->block->Hash().to_string() << " prev: " << block->block->previous.to_string() << " next: " << block->block->next.to_string() << " delegate_id: " << block->delegate_id << std::endl;
	                        finished.insert(j);
	                        continue;
	                    }
	                }
#endif
                    ++c_c_fail;
	                ++fail_count;
	                // RGD1 if we fail one too many times, re-issue the bsb blocks for this micro
	                //      maybe we missed something...
	                block->retry_count++; // How many times this block failed...
	                if(block->retry_count >= BatchBlock::validator::MAX_RETRY || is_black_list_error(rtvl)) {
	                    // Remove block and blacklist...
	                    //finished.insert(i); // RGD1
	                    //p2p::add_to_blacklist(block->peer); // TODO Should we also blacklist all our peers up and to including this one?
	                }
	                LOG_DEBUG(node->log) << "validate failed: hash: " << block->block->Hash().to_string() << " prev: " << block->block->previous.to_string() << " next: " << block->block->next.to_string() << " delegate_id: " << block->delegate_id << std::endl;
                    break;
	            }
	        }
	   }
     }
   }

   // Clean-up the vector removing blocks we processed.
   std::vector<std::shared_ptr<bulk_pull_response> > tmp[NUMBER_DELEGATES];
   for(int j = 0; j < NUMBER_DELEGATES; ++j) {
    for(int i = 0; i < bsb[j].size(); ++i) {
        if(finished_bsb[j].end() == finished_bsb[j].find(i)) {
            tmp[j].push_back(bsb[j][i]);
        }
     }
   }

   // Update the vector with the bsb blocks we still need to process.
   for(int j = 0; j < NUMBER_DELEGATES; ++j) {
        bsb[j] = tmp[j];
   }

   bool ready = false; // logical

   in_memory_micro_tips();

   LOG_DEBUG(node->log) << "remaining: {" << std::endl;
   for(int i = 0; i < micro.size(); ++i) {
        LOG_DEBUG(node->log) << "remaining: " << micro[i]->micro->Hash().to_string() << " prev: " << micro[i]->micro->previous.to_string() << " next: " << micro[i]->micro->next.to_string() << std::endl;
   }
   LOG_DEBUG(node->log) << "remaining: }" << std::endl;

   finished.clear();

   for(int j = 0; j < micro.size(); ++j) {
      std::shared_ptr<ApprovedMB> peerMicro = micro[j]->micro;

      BlockHash peerHash = peerMicro->Hash();
      std::shared_ptr<ApprovedMB> isMicroPresent =  Micro::readMicroBlock(node->store, peerHash);
      if(isMicroPresent == nullptr) {
        //using Request = RequestMessage<MBCT>;
		ValidationStatus rtvl;
        if((!peerMicro->previous.is_zero() || !peerMicro->next.is_zero()) && micro_handler->Validate(*peerMicro, &rtvl)) {
             LOG_DEBUG(node->log) << "micro_handler->Validate: " 
                      << peerMicro->Hash().to_string() << " prev: " << peerMicro->previous.to_string()
                      << " next: " << peerMicro->next.to_string() << std::endl;
             finished.insert(j);
             micro_handler->ApplyUpdates(*peerMicro); // Validation succeeded, add to database.
             // logical
             // Check if we have the last micro in epoch flag
             // as this will signal us to process the next epoch
             if(peerMicro->last_micro_block > 0) {
                ready = true;
             }
            // We applied the current micro (there should only be one
            // micro at any given time) advance to the next micro.
            if(micro.size() == 0) {
                if(attempt) ++attempt->get_next_micro; 
                ++BatchBlock::get_next_micro;
            }
        } else {
             // Try several times, if fail, we assume there is a problem.
             micro[j]->retry_count++; // How many times this block failed...
             if(micro[j]->retry_count >= BatchBlock::validator::MAX_RETRY || is_black_list_error(rtvl)) {
                // Remove block and blacklist...
                finished.insert(j);
                p2p::add_to_blacklist(micro[j]->peer); // TODO Should we also blacklist all our peers up and to including this one?
             }
             LOG_DEBUG(node->log) 
                      << "error validating: " << peerMicro->Hash().to_string() << " prev: " << peerMicro->previous.to_string()
                      << " next: " << peerMicro->next.to_string() << std::endl;
        }
      }
   }

   std::vector<std::shared_ptr<bulk_pull_response_micro> > tmp_micro;
   for(int i = 0; i < micro.size(); ++i) {
        if(finished.end() == finished.find(i)) {
            tmp_micro.push_back(micro[i]);
        }
   }

   // Update the vector with the micro blocks we still need to process.
   micro = tmp_micro;

   if(fail_count > 200 && attempt) {
        //request_micro_block(attempt,micro[0]);
   }

   if(!ready) { // logical
        return false; // Ok, no error...
   }

   in_memory_epoch_tips();

   LOG_DEBUG(node->log) << "remaining epoch: {" << std::endl;
   for(int i = 0; i < epoch.size(); ++i) {
        LOG_DEBUG(node->log) << "remaining epoch: " << epoch[i]->epoch->Hash().to_string() << " prev: " << epoch[i]->epoch->previous.to_string() << " next: " << epoch[i]->epoch->next.to_string() << std::endl;
    }
   LOG_DEBUG(node->log) << "remaining epoch: }" << std::endl;

   finished.clear();
   BlockHash current_micro_hash = Micro::getMicroBlockTip(node->store);
   bool isValid = false;
   for(int j = 0; j < epoch.size(); ++j) {
        //using Request = RequestMessage<ECT>;
        //logos::process_return rtvl;

        ValidationStatus rtvl;
        if(epoch[j]->epoch->micro_block_tip == current_micro_hash && 
           (isValid=epoch_handler->Validate(*(epoch[j]->epoch), &rtvl))) {
            LOG_INFO(node->log) << "epoch_handler->ApplyUpdates: " << epoch[j]->epoch->Hash().to_string() << std::endl;
            epoch_handler->ApplyUpdates(*(epoch[j]->epoch)); // Validation succeeded, add to database.
            finished.insert(j);
            break; // logical, only do one at a time...
       } else if(!isValid) {
             // Try several times, if fail, we assume there is a problem.
             epoch[j]->retry_count++; // How many times this block failed...
             if(epoch[j]->retry_count >= BatchBlock::validator::MAX_RETRY || is_black_list_error(rtvl)) {
                // Remove block and blacklist...
                finished.insert(j);
                p2p::add_to_blacklist(epoch[j]->peer); // TODO Should we also blacklist all our peers up and to including this one?
             }
       } else {
            LOG_DEBUG(node->log) << "epoch_handler->Failed Validation: " << epoch[j]->epoch->micro_block_tip.to_string() << " current: " << current_micro_hash.to_string() << " isValid: " << isValid << std::endl;
       }
   }

   std::vector<std::shared_ptr<bulk_pull_response_epoch> > tmp_epoch;
   for(int i = 0; i < epoch.size(); ++i) {
        if(finished.end() == finished.find(i)) {
            tmp_epoch.push_back(epoch[i]);
        }
   }



   // Update the vector with the micro blocks we still need to process.
   epoch = tmp_epoch;

   return false; // Ok, no error...
}
