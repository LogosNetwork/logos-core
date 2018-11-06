#include <logos/bootstrap/backtrace.hpp>
#include <logos/bootstrap/batch_block_validator.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>
#include <set>

//#define _VALIDATE 1 // FIXME
#define _DEBUG_VALIDATE 1

// CTOR
BatchBlock::validator::validator(logos::node *n)
    : node(n),
      nextMicro(0),
      nextEpoch(0)
{
    // Allocate our epoch/micro validators...
    recall          = std::shared_ptr<RecallHandler>      (new RecallHandler());
    voting_manager  = std::shared_ptr<EpochVotingManager> (new EpochVotingManager(node->store));
    epoch_handler   = std::shared_ptr<EpochHandler>       (new EpochHandler(node->store, 0, *voting_manager));
    micro_handler   = std::shared_ptr<MicroBlockHandler>  (new MicroBlockHandler(node->store, 0, *recall));

#ifdef _TIMEOUT_BOOTSTRAP
    std::thread timeout_thr(BatchBlock::validator::timeout_s, this);
    timeout_thr.detach();
    std::cout << " done BatchBlock::validator init thread " << std::endl;
#endif

#ifdef _DEBUG
    std::cout << " done BatchBlock::validator " << std::endl;
#endif
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
#ifdef _DEBUG
        std::cout << "nr_blocks: " << nr_blocks << std::endl;
#endif
    }
}

bool BatchBlock::validator::validate(std::shared_ptr<BatchBlock::bulk_pull_response> block)
{
    std::lock_guard<std::mutex> lock(mutex);
#ifdef _DEBUG
    std::cout << "validate: bsb.size(): " << bsb.size() << " micro.size(): " << micro.size() << " epoch.size(): " << epoch.size() << std::endl;
#endif
    if(block && (bsb.size() < nr_blocks)) {
        bsb.push_back(block);
        return false;
    } else if(block) {
        bsb.push_back(block);
    } else {
#ifdef _DEBUG
        do_backtrace();
#endif
    }

    // Sort all based on timestamp.
    std::sort(bsb.begin(), bsb.end(),
        [](const std::shared_ptr<BatchBlock::bulk_pull_response> &lhs,
           const std::shared_ptr<BatchBlock::bulk_pull_response> &rhs)
        {
            return lhs->block.timestamp < rhs->block.timestamp;
        }
    );

   // Otherwise, we reached our limit, process each block we received.
   std::set<int> finished;
   for(int i = 0; i < bsb.size(); ++i) {
        std::shared_ptr<BatchBlock::bulk_pull_response> block = bsb[i];
#ifdef _VALIDATE
        if (BatchBlock::Validate(node->_consensus_container,
                block->block,block->delegate_id)) {
                // Block is valid, add to database.
                BatchBlock::ApplyUpdates(node->_consensus_container,
                                 block->block,block->delegate_id);
                finished.insert(i);
        }
#elif _DEBUG_VALIDATE
        if (BatchBlock::Validate(node->store,
                block->block,block->delegate_id)) {
                // Block is valid, add to database.
                BatchBlock::ApplyUpdates(node->store,
                                 block->block,block->delegate_id);
                finished.insert(i);
        }
#else
        finished.insert(i); // TODO: See if we need the container, and maybe we can
                            //       allocate persistence manager locally.
#endif
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

   if(nextMicro >= micro.size()) {
#ifdef _DEBUG
        std::cout << "nextMicro >= micro.size()" << std::endl;
#endif
        // Count number of times this happens, and fail it if exceeds.
        if(nextMicro_counter++ > NEXT_MICRO_COUNTER_MAX) {
            reset();
            return true; // Failed.
        } else {
            return false; // Ok...
        }
    }

    bool ready = true;
    ::MicroBlock *peerMicro = &micro[nextMicro]->micro;
    // Check we have all of our tips, if so create and validate MicroBlock.
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        if(!node->store.state_block_exists(peerMicro->_tips[i])) {
            ready = false; // Not ready, missing a tip.
        }
    }

    if(ready) {
        if(micro_handler->Validate(*peerMicro)) {
             micro_handler->ApplyUpdates(*peerMicro); // Validation succeeded, add to database.
             nextMicro++; // Go to next micro block.
        } else {
             // Try several times, if fail, we assume there is a problem.
             if(micro_validation_error_counter++ > MICRO_VALIDATION_ERROR_COUNTER_MAX) {
                reset();
                return true;
             } else {
                return false;
             }
        }
    } else {
        // Try several times, count number of bsb processed and fail if more than limit.
        if(micro_not_ready_counter++ > MICRO_NOT_READY_COUNTER_MAX) {
            reset();
            return true;
        } else {
            return false;
        }
    }

    if(nextEpoch >= epoch.size()) {
#ifdef _DEBUG
        std::cout << "nextEpoch >= epoch.size()" << std::endl;
#endif
        // TODO: Count number of times this happens, and fail it if exceeds.
        if(nextEpoch_counter++ > NEXT_EPOCH_COUNTER_MAX) {
            reset();
            return true; // Failed.
        } else {
            return false; // Ok...
        }
    }

    ready = false;
    int i = nextMicro - 1, j;
    if(i < 0) {
        reset();
        return true; // Invalid state, shouldn't really happen.
    }

    for(j = 0; j < nextEpoch; ++j) {
        if(epoch[j]->epoch._micro_block_tip == micro[i]->micro.hash()) {
            ready = true;
            break;
        }
    }

    if(ready) {
        if(epoch_handler->Validate(epoch[j]->epoch)) {
            epoch_handler->ApplyUpdates(epoch[j]->epoch); // Validation succeeded, add to database.
            nextEpoch++;
        }
    } else {
        // TODO: Try several times, count number of failures, if reached a max, assume failure.
        if(epoch_not_ready_counter++ > EPOCH_NOT_READY_COUNTER_MAX) {
            reset();
            return true;
        } else {
            return false; 
        }
    }

    return false; // Ok, no error...
}
