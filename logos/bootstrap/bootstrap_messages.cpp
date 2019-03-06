#include <iostream>
#include <logos/blockstore.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/attempt.hpp>
#include <logos/bootstrap/bootstrap.hpp>

int BatchBlock::get_next_micro = 0;

BlockHash BatchBlock::getBatchBlockTip(Store& store, int delegate)
{
    BlockHash hash = 0;
    if(!store.batch_tip_get(delegate,hash)) {
        return hash;
    }
    return hash;
}

uint64_t  BatchBlock::getBatchBlockSeqNr(Store& store, int delegate)
{
    // The below code will dump core in bsb dtor.
    // Also, the sequence number is not correct. Need real world database to test.
    ApprovedBSB batch;
    BlockHash hash = BatchBlock::getBatchBlockTip(store,delegate);
    if(hash.is_zero()) {
        return BatchBlock::NOT_FOUND;
    }
    if(store.batch_block_get(hash, batch)) {
        return BatchBlock::NOT_FOUND;
    }
    return batch.sequence;
}

bool BatchBlock::tips_response::CanProceed(BatchBlock::tips_response & resp)//TODO sqn change
{
    LOG_DEBUG(logos::bootstrap_get_logger()) << "CanProceed mine: " << *this << " theirs: " << resp << std::endl;

    if(epoch_block_seq_number >= resp.epoch_block_seq_number &&
       micro_block_seq_number >= resp.micro_block_seq_number) {
       for(int i = 0; i < NUMBER_DELEGATES; i++) {
            if(resp.batch_block_seq_number[i] != BatchBlock::NOT_FOUND &&
               batch_block_seq_number[i] < resp.batch_block_seq_number[i]) {
                return false;
            } 
       }
       return true; // Ok, proceed
    } else {
        return false;
    }
}

//TODO sqn change
void BatchBlock::tips_response::PopulateLogical(Store & store, BatchBlock::tips_response & resp) // logical
{
    BlockHash epoch_tip = 0;
    BlockHash micro_tip = 0;
    uint32_t  epoch_seq = 0;
    uint32_t  micro_seq = 0;

    // Get next micro from client's
    micro_tip = Micro::getNextMicroBlock(store, resp.micro_block_tip);
    auto m = Micro::readMicroBlock(store, micro_tip);
    if(m == nullptr) {
        // Nothing to advance to...
        micro_tip = resp.micro_block_tip;
        micro_seq = resp.micro_block_seq_number;
    } else {
        // Get two at a time unless we are at the last one
        if(!m->next.is_zero()) {
            micro_tip = Micro::getNextMicroBlock(store, m->next);
            m = Micro::readMicroBlock(store, m->next);
            micro_seq = m->sequence;
        } else {
            micro_seq = m->sequence;
        }
    }
    micro_tip = Micro::getMicroBlockTip(store); // RGD Hack
    m = Micro::readMicroBlock(store, micro_tip);
    if (m != nullptr && /* assert we have a new micro to visit */
        (m->last_micro_block > 0 /* at the end micro in an epoch */ || 
        resp.epoch_block_seq_number <= 2 /* at the beginning of epochs */)) {
        // Advance the epoch
        epoch_tip = EpochBlock::getNextEpochBlock(store, resp.epoch_block_tip);
        auto e = EpochBlock::readEpochBlock(store, epoch_tip);
        if(e) {
            epoch_seq = e->epoch_number;
        }
    } else {
        // Stay in same epoch
        epoch_tip = resp.epoch_block_tip;
        epoch_seq = resp.epoch_block_seq_number;
    }
    epoch_block_tip        = epoch_tip;
    micro_block_tip        = micro_tip;
    epoch_block_seq_number = epoch_seq;
    micro_block_seq_number = micro_seq;

    Micro::dumpMicroBlockTips(store,micro_tip);

    // NOTE Get the tips for all delegates and send them one by one for processing...
    for(int i = 0; i < NUMBER_DELEGATES; i++)
    {
        BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(store, i);
        uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(store, i);
        // Fill in the response...
        batch_block_tip[i]        = bsb_tip;
        batch_block_seq_number[i] = bsb_seq;
    }

    delegate_id = 0;
    timestamp_start = 0;
    timestamp_end = 0;
}

void BatchBlock::tips_response::Populate(Store & store)
{
    BlockHash epoch_tip = EpochBlock::getEpochBlockTip(store);
    BlockHash micro_tip = Micro::getMicroBlockTip(store);
    uint32_t  epoch_seq = EpochBlock::getEpochBlockSeqNr(store);
    uint32_t  micro_seq = Micro::getMicroBlockSeqNr(store);
    epoch_block_tip        = epoch_tip;
    micro_block_tip        = micro_tip;
    epoch_block_seq_number = epoch_seq;
    micro_block_seq_number = micro_seq;

    Micro::dumpMicroBlockTips(store,micro_tip);

    // NOTE Get the tips for all delegates and send them one by one for processing...
    for(int i = 0; i < NUMBER_DELEGATES; i++)
    {
        BlockHash bsb_tip   = BatchBlock::getBatchBlockTip(store, i);
        uint32_t  bsb_seq   = BatchBlock::getBatchBlockSeqNr(store, i);
        // Fill in the response...
        batch_block_tip[i]        = bsb_tip;
        batch_block_seq_number[i] = bsb_seq;
    }

    delegate_id = 0;
    timestamp_start = 0;
    timestamp_end = 0;
}
