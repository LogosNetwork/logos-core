#pragma once

#include <memory>
#include <mutex>

#include <logos/lib/numbers.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/bootstrap/bulk_pull_response.hpp>
#include <logos/bootstrap/batch_block_tips.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>
#include <logos/consensus/persistence/epoch/nondel_epoch_persistence.hpp>
#include <logos/consensus/persistence/microblock/nondel_microblock_persistence.hpp>

#include <logos/epoch/epoch.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/epoch/recall_handler.hpp>
#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>

#include <logos/microblock/microblock.hpp>
#include <logos/microblock/microblock_handler.hpp>


namespace BatchBlock {
    class validator;
}

#include <logos/node/node.hpp>

#include <logos/bootstrap/epoch.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/lib/trace.hpp>

namespace BatchBlock {

inline
const std::string currentDateTime() 
{
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}

class validator {

    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response> > bsb[NUMBER_DELEGATES]; // Batch State Blocks received.
    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_micro> > micro; // Micro.
    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_epoch> > epoch; // Epoch.
    std::shared_ptr<BatchBlock::tips_response> tips;

    static constexpr int NR_BLOCKS = 4096; // Max blocks we wait for before processing.

    logos::node *node;

    uint64_t nr_blocks;
    uint64_t nextMicro;
    uint64_t nextEpoch;

    std::mutex mutex;

    // For validation of epoch/micro blocks.
    shared_ptr<NonDelPersistenceManager<ECT> >  epoch_handler;
    shared_ptr<NonDelPersistenceManager<MBCT> > micro_handler;
 
    static int constexpr MAX_RETRY                          = 1000;

    public:

    static constexpr int TIMEOUT = 10; // seconds.
    static void timeout_s(BatchBlock::validator *this_l);

    /// Class constructor
    /// @param node pointer (needed to access logging, etc)
    validator(logos::node *n);

    /// Class destructor
    virtual ~validator();

    bool can_proceed() // logical //TODO
    {
        //return true; // RGD
        std::lock_guard<std::mutex> lock(mutex);
        return true; // Hack...
        if(micro.size() <= 3) { // Was micro.empty()
            return true; // No pending micros need to be processed, ok to continue...
        } else {
            std::cout << "can_proceed:: micro.size(): " << micro.size() << std::endl;
            for(int i = 0; i < micro.size(); ++i) {
                std::cout << "can_proceed:: blocked on hash: " << micro[i]->micro->Hash().to_string() << std::endl;
            }
            return false;
        }
    }

    /// add_tips_response
    /// Store the tip response in a given request
    void add_tips_response(std::shared_ptr<BatchBlock::tips_response> &resp)
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        tips = resp;
    }

    /// add_micro_block callback where a new micro block arriving from 
    ///                 the network is saved to the queue
    /// @param m shared pointer of bulk_pull_response_micro
    void add_micro_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m);
    void request_micro_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m);

    /// add_epoch_block callback where a new epoch block arriving from 
    ///                 the network is saved to the queue
    /// @param e shared pointer of bulk_pull_response_epoch
    void add_epoch_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_epoch> &e);

    /// clear_micro_block clears our micro block queue
    void clear_micro_block()
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        micro.clear();
    }

    /// clear_epoch_block clears our epoch block queue
    void clear_epoch_block()
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        epoch.clear();
    }

    // Report what we have in our queues so we don't re-request the same blocks...

    /// in_memory_bsb_tips the current bsb tips in memory (not in database yet)
    std::map<int, std::pair<int64_t, BlockHash> >  in_memory_bsb_tips();//all the blocks in the cache

    /// in_memory_micro_tips the current micro block tips in memory (not in database yet)
    std::pair<int64_t, BlockHash> in_memory_micro_tips();

    /// in_memory_epoch_tips the current epoch block tips in memory (not in database yet)
    std::pair<int64_t, BlockHash> in_memory_epoch_tips();

    /// is_black_list_error
    /// @param result of validation of a block
    /// @returns true if the result implies we blacklist the peer
    bool is_black_list_error(ValidationStatus & result)//TODO error type
    {
#if 0
        if(result.reason == logos::process_result::progress         ||
           result.reason == logos::process_result::old              ||
           result.reason == logos::process_result::gap_previous     ||
           result.reason == logos::process_result::gap_source       ||
           result.reason == logos::process_result::buffered         ||
           result.reason == logos::process_result::buffering_done   ||
           result.reason == logos::process_result::pending          ||
           result.reason == logos::process_result::fork
        ) {
            return false; // Allow...
        }
        return true; // Fail others...
#endif
        return false; // TODO This can't be right, we have no way of knowing why...
    }

    /// validate validation logic for bsb, micro and epoch
    /// @param block shared pointer of bulk_pull_response
    /// @returns boolean (true if failed, false if success)
    bool validate(std::shared_ptr<logos::bootstrap_attempt> attempt, std::shared_ptr<bulk_pull_response> block);

};

} // namespace BatchBlock
