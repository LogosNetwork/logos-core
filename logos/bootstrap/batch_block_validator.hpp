#pragma once

#include <memory>
#include <mutex>

#include <logos/lib/numbers.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/bootstrap/batch_block_bulk_pull.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>

#include <logos/epoch/epoch.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/epoch/recall_handler.hpp>

#include <logos/microblock/microblock.hpp>
#include <logos/microblock/microblock_handler.hpp>


namespace BatchBlock {
    class validator;
}

#include <logos/node/node.hpp>

#include <logos/bootstrap/epoch.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/backtrace.hpp>

namespace BatchBlock {

class validator {

    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response> > bsb; // Batch State Blocks received.
    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_micro> > micro; // Micro.
    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_epoch> > epoch; // Epoch.

    static constexpr int NR_BLOCKS = 4096; // Max blocks we wait for before processing.

    logos::node *node;

    uint64_t nextMicro;
    uint64_t nextEpoch;

    std::mutex mutex;

    // For validation of epoch/micro blocks.
    shared_ptr<RecallHandler>       recall;
    shared_ptr<EpochVotingManager>  voting_manager;
    shared_ptr<EpochHandler>        epoch_handler;
    shared_ptr<MicroBlockHandler>   micro_handler;
 
    uint64_t nextMicro_counter;
    uint64_t nextEpoch_counter;
    uint64_t micro_validation_error_counter;
    uint64_t epoch_validation_error_counter;
    uint64_t micro_not_ready_counter;
    uint64_t epoch_not_ready_counter;

    // TODO: Ask for valid values.
    static int constexpr NEXT_MICRO_COUNTER_MAX             = 100;
    static int constexpr NEXT_EPOCH_COUNTER_MAX             = 100;
    static int constexpr MICRO_VALIDATION_ERROR_COUNTER_MAX = 100;
    static int constexpr EPOCH_VALIDATION_ERROR_COUNTER_MAX = 100;
    static int constexpr MICRO_NOT_READY_COUNTER_MAX        = 100;
    static int constexpr EPOCH_NOT_READY_COUNTER_MAX        = 100;

    public:

    static constexpr int TIMEOUT = 10; // seconds.
    static void timeout_s(BatchBlock::validator *this_l);

    // CTOR
    validator(logos::node *n);
    virtual ~validator();

    bool reset();

    void add_micro_block(std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m)
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        micro.push_back(m);
    }

    void add_epoch_block(std::shared_ptr<BatchBlock::bulk_pull_response_epoch> &e)
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        epoch.push_back(e);
    }

    void clear_micro_block()
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        micro.clear();
    }

    void clear_epoch_block()
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        epoch.clear();
    }

    // TODO: When we get NR_BLOCKS we:
    //       sort
    //       validate
    //       apply update
    //       if ok, we create merkle tree
    //       if at end of tree, produce micro block
    //       store micro blocks on a queue
    //       if at end of a epoch block, produce epoch block
    //       store epoch block on a queue
    bool validate(std::shared_ptr<bulk_pull_response> block);
};

} // namespace BatchBlock
