#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/consensus/messages/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/consensus_manager.hpp>
#include <logos/consensus/consensus_container.hpp>
#include <logos/consensus/persistence/persistence_manager.hpp>

#define NUMBER_DELEGATES 32

namespace BatchBlock {

    using Store = ConsensusContainer::Store;

enum tips_protocol {
    tips_RESPONSE  = 66,
};

struct tips_response {
    const logos::block_type block_type = logos::block_type::frontier_block;
    char pad[3]={0}; // RGD
    const int process_code = tips_RESPONSE;
    uint64_t timestamp_start;
    uint64_t timestamp_end;
    int delegate_id;
    BlockHash epoch_block_tip;
    BlockHash micro_block_tip;
    BlockHash batch_block_tip; // [NUMBER_DELEGATES];
    uint32_t epoch_block_seq_number;
    uint32_t micro_block_seq_number;
    uint32_t batch_block_seq_number; // [NUMBER_DELEGATES];

    friend
    ostream& operator<<(ostream &out, BatchBlock::tips_response resp)
    {
        out << "block_type: tips_block timestamp_start: " << resp.timestamp_start
            << " timestamp_end: " << resp.timestamp_end
            << " delegate_id: "   << resp.delegate_id
            << " epoch_block_tip: [" << resp.epoch_block_tip.to_string() << "] "
            << " micro_block_tip: [" << resp.micro_block_tip.to_string() << "] "
            << " batch_block_tip: [" << resp.batch_block_tip.to_string() << "] "
            << " epoch_block_seq_number: " << resp.epoch_block_seq_number
            << " micro_block_seq_number: " << resp.micro_block_seq_number
            << " batch_block_seq_number: " << resp.batch_block_seq_number
            << "\n";
        return out;
    }
};

constexpr int tips_response_mesg_len = (sizeof(tips_response));

BlockHash getBatchBlockTip(Store &s, int delegate);
uint32_t  getBatchBlockSeqNr(Store &s, int delegate);

} // namespace BatchBlock
