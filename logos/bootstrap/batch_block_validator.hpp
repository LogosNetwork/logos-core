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

namespace BatchBlock {
    class validator;
}

#include <logos/node/node.hpp>

namespace BatchBlock {

class EpochBlock; // TODO Use correct struct when available...

class validator {

    std::vector<std::shared_ptr<bulk_pull_response> > bsb; // Batch State Blocks received.
    std::vector<std::shared_ptr<BatchBlock::bulk_pull_response_micro> > micro;       // Micro.
    std::vector<std::shared_ptr<EpochBlock> > epoch;       // Epoch.

    static constexpr int NR_BLOCKS = 65536;     // Max blocks we wait for before processing.
    logos::node *node;
    uint64_t nextMicro;
    uint64_t nextEpoch;
    std::mutex mutex;

    // TODO Add Merkle tree
    //      Add micro blocks queue
    //      Add epoch blocks queue
    //      Add get methods for micro and epoch blocks
    //      DONE.

    public:

    static constexpr int TIMEOUT = 10; // seconds.
    static void timeout_s(BatchBlock::validator *this_l);

    // CTOR
    validator(logos::node *n);

    // TODO init()
    //      determine our current micro block
    //      walk the tree constructing merkle tree up to tip
    //      make this tree available for validate()
    //      -> actually, get the vector of MicroBlock and Epoch block
    //      -> Put those queues on bootstrap_client (some place globally accessible and populate when get the epoch and the micro blocks from peer)
    //         actually, we can put it here since we are on the node and therefore should be accessible by anything in bootstrap.
    bool init();

    void add_micro_block(std::shared_ptr<BatchBlock::bulk_pull_response_micro> &m)
    {
        std::lock_guard<std::mutex> lock(mutex); // DEBUG Might need additional locking in this class...
        micro.push_back(m);
    }

    void add_epoch_block(std::shared_ptr<EpochBlock> &e)
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
