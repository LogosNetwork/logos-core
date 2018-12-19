#include <logos/bootstrap/batch_block_validator.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>
#include <set>

// CTOR
BatchBlock::validator::validator(logos::node *n)
    : node(n),
      nextMicro(0),
      nextEpoch(0)
{
    // Allocate our epoch/micro validators...
    epoch_handler = make_shared<PersistenceManager<ECT> >(PersistenceManager<ECT>(node->store,nullptr));
    micro_handler = make_shared<PersistenceManager<MBCT> >(PersistenceManager<MBCT>(node->store,nullptr));

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
    BlockHash zero;
    if(micro.size() <= 0) {
        return std::make_pair(-1,zero);
    }
    std::sort(micro.begin(), micro.end(),
        [](const std::shared_ptr<BatchBlock::bulk_pull_response_micro> &lhs,
           const std::shared_ptr<BatchBlock::bulk_pull_response_micro> &rhs)
        {
            return lhs->micro.sequence < rhs->micro.sequence;
        }
    );

    return std::make_pair(micro[micro.size()-1]->micro.sequence, micro[micro.size()-1]->micro.Hash());
}

std::pair<int64_t, BlockHash> BatchBlock::validator::in_memory_epoch_tips()
{
    BlockHash zero;
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
                LOG_INFO(node->log) << "validate successful: hash: " << block->block.Hash().to_string() << " prev: " << block->block.previous.to_string() << " next: " << block->block.next.to_string() << " delegate_id: " << block->delegate_id << std::endl;
                finished.insert(i);
        } else {
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
        LOG_DEBUG(node->log) << "remaining: " << micro[i]->micro.hash().to_string() << " prev: " << micro[i]->micro.previous.to_string() << " next: " << micro[i]->micro.next.to_string() << std::endl;
   }
   LOG_DEBUG(node->log) << "remaining: }" << std::endl;

   for(int j = 0; j < micro.size(); ++j) {
      ::MicroBlock *peerMicro = &micro[j]->micro;

      BlockHash peerHash = peerMicro->hash();
      std::shared_ptr<MicroBlock> isMicroPresent =  Micro::readMicroBlock(node->store, peerHash);
      if(ready && isMicroPresent == nullptr) {
        using Request = RequestMessage<MBCT>;
        if((!peerMicro->previous.is_zero() || !peerMicro->next.is_zero()) && micro_handler->Validate(static_cast<const Request&>(*peerMicro))) {
             LOG_DEBUG(node->log) << "micro_handler->Validate: " 
                      << peerMicro->hash().to_string() << " prev: " << peerMicro->previous.to_string()
                      << " next: " << peerMicro->next.to_string() << std::endl;
             micro_handler->ApplyUpdates(static_cast<const Request&>(*peerMicro)); // Validation succeeded, add to database.
        } else {
             // Try several times, if fail, we assume there is a problem.
             LOG_DEBUG(node->log) 
                      << "error validating: " << peerMicro->hash().to_string() << " prev: " << peerMicro->previous.to_string()
                      << " next: " << peerMicro->next.to_string() << std::endl;
        }
      }
   }

   in_memory_epoch_tips();

   LOG_DEBUG(node->log) << "remaining epoch: {" << std::endl;
   for(int i = 0; i < epoch.size(); ++i) {
        LOG_DEBUG(node->log) << "remaining epoch: " << epoch[i]->epoch.hash().to_string() << " prev: " << epoch[i]->epoch.previous.to_string() << " next: " << epoch[i]->epoch.next.to_string() << std::endl;
    }
   LOG_DEBUG(node->log) << "remaining epoch: }" << std::endl;

   BlockHash current_micro_hash = Micro::getMicroBlockTip(node->store);
   bool isValid = false;
   for(int j = 0; j < epoch.size(); ++j) {
        using Request = RequestMessage<ECT>;
        logos::process_return rtvl;
        if(epoch[j]->epoch.micro_block_tip == current_micro_hash && 
           (isValid=epoch_handler->Validate(static_cast<const Request&>(epoch[j]->epoch),rtvl))) {
            LOG_INFO(node->log) << "epoch_handler->ApplyUpdates: " << epoch[j]->epoch.hash().to_string() << std::endl;
            epoch_handler->ApplyUpdates(static_cast<const Request&>(epoch[j]->epoch)); // Validation succeeded, add to database.
       } else {
            LOG_DEBUG(node->log) << "epoch_handler->Failed Validation: " << epoch[j]->epoch.micro_block_tip.to_string() << " current: " << current_micro_hash.to_string() << " isValid: " << isValid << std::endl;
       }
   }

   return false; // Ok, no error...
}
