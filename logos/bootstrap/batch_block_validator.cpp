#include <logos/bootstrap/batch_block_validator.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/backtrace.hpp>

//#define _VALIDATE 1 // RGD Hack
#define _DEBUG 1

// CTOR
BatchBlock::validator::validator(logos::node *n)
    : node(n),
      nextMicro(0),
      nextEpoch(0)
{
#ifdef _TIMEOUT_BOOTSTRAP
    std::thread timeout_thr(BatchBlock::validator::timeout_s, this);
    timeout_thr.detach();
    std::cout << " done BatchBlock::validator init thread " << std::endl;
#endif
#ifdef _DEBUG
    std::cout << " done BatchBlock::validator " << std::endl;
#endif
}

void timeout_s(BatchBlock::validator *this_l)
{
    while(true) {
        sleep(BatchBlock::validator::TIMEOUT);
        this_l->validate(nullptr); // Force us to drain the queue...
    }
}

bool BatchBlock::validator::init()
{
    nextMicro = nextEpoch = 0;
}

bool BatchBlock::validator::validate(std::shared_ptr<BatchBlock::bulk_pull_response> block)
{
    static int instance = 0;
    std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
    // RGDTODO Put a time out here using an alarm.
    if((bsb.size() < NR_BLOCKS) && block) {
        int size = bsb.size();
        bsb.push_back(block);
#ifdef _DEBUG
        std::cout << "BatchBlock::validator::validate<1>: " << instance++ << " bsb.size(): " << bsb.size() << " NR_BLOCKS: " << NR_BLOCKS << " this: " << this << std::endl;
#endif
        return true;
    }
#ifdef _DEBUG
    std::cout << "BatchBlock::validator::validate<2>: " << instance++ << " bsb.size(): " << bsb.size() << " NR_BLOCKS: " << NR_BLOCKS << " this: " << this << " block==nullptr: " << (block == nullptr) << std::endl;
#endif
    // Add our latest block...
    if(block) {
        bsb.push_back(block);
    } else {
        do_backtrace();
    }
    // Sort based on timestamp.
    std::sort(bsb.begin(), bsb.end(),
              [](const std::shared_ptr<BatchBlock::bulk_pull_response> &lhs, const std::shared_ptr<BatchBlock::bulk_pull_response> &rhs)
              {
                    return lhs->block.timestamp < rhs->block.timestamp;
              }
    );
    // Otherwise, we reached our limit, process each block we received.
    for(int i = 0; i < bsb.size(); ++i) {
        std::shared_ptr<BatchBlock::bulk_pull_response> block = bsb[i];
#ifdef _VALIDATE
        if (BatchBlock::Validate(node->_consensus_container,
            block->block,block->delegate_id)) {
#endif
            // Block is valid, add to database.
            //BatchBlock::ApplyUpdates(node->_consensus_container, // RGD Hack
            //                     block->block,block->delegate_id);
#ifdef _VALIDATE
        } else {
            // TODO:
            // rollback db transactions ?
            // bootstrap from another peer ?
            // TODOCACHE
            // leave it in the queue for processing later
            // unless we reach the end, and still have blocks to be processed
            // either because block is null or we reach the end of queue and still have
            // to process it
            if(block == nullptr) { // FIXME
                bsb.clear(); // Processed all the blocks.
                return true; // Error....
            }
            continue; // Process the next one, likely a dependency thing...
        }
#endif
    }
    if(nextMicro >= micro.size()) {
#ifdef _DEBUG
        std::cout << "nextMicro >= micro.size()" << std::endl;
#endif
        // TODO:
        // Handle epoch block...
        return false; // Ok...
    }
    bool ready = true;
    ::MicroBlock *peerMicro = &micro[nextMicro]->micro;
    // Check we have all of our tips, if so create and validate MicroBlock.
    for(int i = 0; i < NUMBER_DELEGATES; ++i) {
        if(!node->store.state_block_exists(peerMicro->tips[i])) {
            ready = false; // Not ready, missing a tip.
        }
    }
    if(ready) {
#ifdef _DEBUG
        std::cout << "validator->microBlock" << std::endl;
#endif
        ::MicroBlock newMicro;
        ::MicroBlockHandler microHandler(node->alarm,node->store,NUMBER_DELEGATES,std::chrono::seconds(-1)); // ASK GREG, need to instantiate and call BuildMicroBlock ourselves...
        microHandler.BuildMicroBlock(newMicro);
        if(newMicro.merkleRoot == peerMicro->merkleRoot) {
             // store in db
             logos::transaction transaction(node->store.environment, nullptr, true);
             BlockHash hash = node->store.micro_block_put(newMicro,transaction);
             node->store.micro_block_tip_put(hash,transaction);
             nextMicro++; // Go to next micro block.
        } else {
             // TODO:
             // rollback db transactions ?
             // bootstrap from another peer ?
#ifdef _DEBUG
             std::cout << " validator->rollback: " << std::endl;
#endif
             return true;
        }
    }
    // TODO Handle epoch block here...
#ifdef _DEBUG
    std::cout << " validator->bsb.clear: " << std::endl;
#endif
    bsb.clear(); // Processed all the blocks.
    return false; // Ok, no error...
}
