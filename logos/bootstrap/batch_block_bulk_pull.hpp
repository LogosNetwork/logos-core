#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>

#include <logos/bootstrap/batch_block_frontier.hpp>

//class MicroBlock { public: uint8_t delegateNumber; BlockHash Hash() { return BlockHash(); } }; // TODO: Use Greg's code...

namespace BatchBlock {

enum bulk_pull_protocol {
    BULK_PULL_REQUEST  = 64,
    BULK_PULL_RESPONSE = 65,
};

struct bulk_pull_request {
    const int process_code = BULK_PULL_REQUEST;
    int delegate_id;
    uint64_t timestamp;
    logos::block_hash hash;
};

struct bulk_pull_response {
    const logos::block_type block_type = logos::block_type::batch_block;
    char pad[3]={0}; // RGD
    const int process_code = BULK_PULL_RESPONSE;
    int delegate_id;
    BatchStateBlock block;
};

struct bulk_pull_response_micro { // RGD EMERGENCY How do we know what we are sending if we have two seperate types of blocks ? How do we know how much to read ? Put it all in one block...
    const logos::block_type block_type = logos::block_type::micro_block;
    char pad[3]={0}; // RGD
    const int process_code = BULK_PULL_RESPONSE;
    int delegate_id;
    MicroBlock micro;
};

constexpr int bulk_pull_response_mesg_len = (sizeof(bulk_pull_response) + sizeof(bulk_pull_response_micro));

bool Validate(ConsensusContainer &manager, BatchStateBlock &block, int delegate_id);
void ApplyUpdates(ConsensusContainer &manager, const BatchStateBlock & message, uint8_t delegate_id);

BlockHash getNextBatchStateBlock(Store &store, int delegate, BlockHash &h);
std::shared_ptr<BatchStateBlock> readBatchStateBlock(Store &store, BlockHash &h);

} // BatchBlock
