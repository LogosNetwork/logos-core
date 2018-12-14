#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>

#include <logos/bootstrap/batch_block_tips.hpp>

namespace BatchBlock {

const int BULK_PULL_RESPONSE = 65;

struct bulk_pull_response {
    const logos::block_type block_type = logos::block_type::batch_block;
    char pad[3]={0}; // NOTE
    const int process_code = BULK_PULL_RESPONSE;
    int delegate_id;
    BatchStateBlock block;
};

struct bulk_pull_response_micro {
    const logos::block_type block_type = logos::block_type::micro_block;
    char pad[3]={0}; // NOTE 
    const int process_code = BULK_PULL_RESPONSE;
    int delegate_id;
    MicroBlock micro;
};

struct bulk_pull_response_epoch {
    const logos::block_type block_type = logos::block_type::epoch_block;
    char pad[3]={0}; // NOTE 
    const int process_code = BULK_PULL_RESPONSE;
    int delegate_id;
    Epoch epoch;
};

constexpr int bulk_pull_response_mesg_len = (sizeof(bulk_pull_response) + sizeof(bulk_pull_response_micro) + sizeof(bulk_pull_response_epoch));

bool Validate(Store & store, const BatchStateBlock & message, int delegate_id);
void ApplyUpdates(Store & store, const BatchStateBlock & message, uint8_t delegate_id);

BlockHash getNextBatchStateBlock(Store &store, int delegate, BlockHash &h);
std::shared_ptr<BatchStateBlock> readBatchStateBlock(Store &store, BlockHash &h);
BlockHash getPrevBatchStateBlock(Store &store, int delegate, BlockHash &h);

} // BatchBlock
