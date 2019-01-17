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
#include <logos/consensus/persistence/persistence_manager.hpp>

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

    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response> > bsb; // Batch State Blocks received.
    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_micro> > micro; // Micro.
    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_epoch> > epoch; // Epoch.
    std::vector<std::shared_ptr<BatchBlock::tips_response> > tips; // Tips.

    static constexpr int NR_BLOCKS = 4096; // Max blocks we wait for before processing.

    logos::node *node;

    uint64_t nr_blocks;
    uint64_t nextMicro;
    uint64_t nextEpoch;

    std::mutex mutex;

    // For validation of epoch/micro blocks.
    shared_ptr<PersistenceManager<ECT> >  epoch_handler;
    shared_ptr<PersistenceManager<MBCT> > micro_handler;
 
    uint64_t nextMicro_counter;
    uint64_t nextEpoch_counter;
    uint64_t micro_validation_error_counter;
    uint64_t epoch_validation_error_counter;
    uint64_t micro_not_ready_counter;
    uint64_t epoch_not_ready_counter;

    static int constexpr NEXT_MICRO_COUNTER_MAX             = 100;
    static int constexpr NEXT_EPOCH_COUNTER_MAX             = 100;
    static int constexpr MICRO_VALIDATION_ERROR_COUNTER_MAX = 100;
    static int constexpr EPOCH_VALIDATION_ERROR_COUNTER_MAX = 100;
    static int constexpr MICRO_NOT_READY_COUNTER_MAX        = 100;
    static int constexpr EPOCH_NOT_READY_COUNTER_MAX        = 100;
    static int constexpr MAX_BSB_RETRY                      = 1000;

    public:

    static constexpr int TIMEOUT = 10; // seconds.
    static void timeout_s(BatchBlock::validator *this_l);

    /// Class constructor
    /// @param node pointer (needed to access logging, etc)
    validator(logos::node *n);

    /// Class destructor
    virtual ~validator();

    /// reset clears state of class
    /// @param none
    bool reset();

    /// add_tips_response
    /// Store the tip response in a given request
    void add_tips_response(std::shared_ptr<BatchBlock::tips_response> &resp)
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        tips.clear(); // Store most recent tips only.
        tips.push_back(resp);
    }

    /// add_micro_block callback where a new micro block arriving from 
    ///                 the network is saved to the queue
    /// @param m shared pointer of bulk_pull_response_micro
    void add_micro_block(std::shared_ptr<logos::bootstrap_attempt> &attempt, std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m);

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
    std::map<int, std::pair<int64_t, BlockHash> >  in_memory_bsb_tips();

    /// in_memory_micro_tips the current micro block tips in memory (not in database yet)
    std::pair<int64_t, BlockHash> in_memory_micro_tips();

    /// in_memory_epoch_tips the current epoch block tips in memory (not in database yet)
    std::pair<int64_t, BlockHash> in_memory_epoch_tips();

    /// validate validation logic for bsb, micro and epoch
    /// @param block shared pointer of bulk_pull_response
    /// @returns boolean (true if failed, false if success)
    bool validate(std::shared_ptr<bulk_pull_response> block);
};

} // namespace BatchBlock
