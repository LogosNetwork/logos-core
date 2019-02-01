#pragma once

#include <logos/lib/numbers.hpp>
#include <logos/lib/blocks.hpp>

#define NUMBER_DELEGATES 32

namespace BatchBlock {

using Store = logos::block_store;
using BlockHash = logos::block_hash;

const int NOT_FOUND = -1;

enum tips_protocol {
    tips_RESPONSE  = 66,
};

struct tips_response {
    const logos::block_type block_type = logos::block_type::frontier_block;
    char pad[3]={0}; // Note
    const int process_code = tips_RESPONSE;
    uint64_t timestamp_start;
    uint64_t timestamp_end;
    int delegate_id;
    BlockHash epoch_block_tip;
    BlockHash micro_block_tip;
    BlockHash batch_block_tip[NUMBER_DELEGATES];
    uint32_t epoch_block_seq_number;
    uint32_t micro_block_seq_number;
    uint32_t batch_block_seq_number[NUMBER_DELEGATES];

    /// Class constructor
    tips_response()
        : timestamp_start(0),
          timestamp_end(0),
          delegate_id(0),
          epoch_block_seq_number(0),
          micro_block_seq_number(0)          
    {
        BlockHash zero  = 0;
        epoch_block_tip = zero;
        micro_block_tip = zero;
        for(int i = 0; i < NUMBER_DELEGATES; i++) {
            batch_block_tip[i] = zero;
            batch_block_seq_number[i] = 0;
        }
    }

    /// CanProceed
    /// @param tips_response
    /// @returns true if we are ahead, false otherwise
    bool CanProceed(tips_response & resp);

    /// Populate
    /// Gets the data from our database and fills this structure with it
    /// @param store BlockStore
    void Populate(Store & store);

    /// Populate
    /// Gets the data from our database and fills this structure with it
    /// proceeds one by one, given the tips from the client, we get the next
    /// micro, and if we are at the last_micro_block, we send a new epoch
    /// @param store BlockStore
    void PopulateLogical(Store & store, BatchBlock::tips_response & resp);

    /// Serialize
    /// write this object out as a stream.
    void Serialize(logos::stream & stream)
    {
        logos::write(stream, block_type); 
        logos::write(stream, pad[0]);
        logos::write(stream, pad[1]);
        logos::write(stream, pad[2]);
        logos::write(stream, timestamp_start);
        logos::write(stream, timestamp_end);
        logos::write(stream, delegate_id);
        logos::write(stream, epoch_block_tip);
        logos::write(stream, micro_block_tip);
        for(int i = 0; i < NUMBER_DELEGATES; i++) {
            logos::write(stream, batch_block_tip[i]);
        }
        logos::write(stream, epoch_block_seq_number);
        logos::write(stream, micro_block_seq_number);
        for(int i = 0; i < NUMBER_DELEGATES; i++) {
            logos::write(stream, batch_block_seq_number[i]);
        }
    }

    /// DeSerialize
    /// @param stream
    /// @param tips_response object to create
    /// create object from stream
    static bool DeSerialize(logos::stream & stream, tips_response & resp) {
        bool error = false;
        char block_type = 0;
        error = logos::read(stream, block_type);
        if(error) {
            return error;
        }
        if((logos::block_type)block_type != resp.block_type) {
            return true; // error
        }
        error = logos::read(stream, resp.pad[0]);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.pad[1]);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.pad[2]);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.timestamp_start);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.timestamp_end);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.delegate_id);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.epoch_block_tip);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.micro_block_tip);
        if(error) {
            return error;
        }
        for(int i = 0; i < NUMBER_DELEGATES; i++) {
            error = logos::read(stream, resp.batch_block_tip[i]);
            if(error) {
                return error;
            }
        }
        error = logos::read(stream, resp.epoch_block_seq_number);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.micro_block_seq_number);
        if(error) {
            return error;
        }
        for(int i = 0; i < NUMBER_DELEGATES; i++) {
            logos::read(stream, resp.batch_block_seq_number[i]);
            if(error) {
                return error;
            }
        }
    }

    /// ostream operator
    /// @param out ostream reference
    /// @param resp BatchBlock::tips_response (object to stream out)
    /// @returns ostream operator
    friend
    ostream& operator<<(ostream &out, BatchBlock::tips_response resp)
    {
        out << "block_type: tips_block timestamp_start: " << resp.timestamp_start
            << " timestamp_end: " << resp.timestamp_end
            << " delegate_id: "   << resp.delegate_id
            << " epoch_block_tip: [" << resp.epoch_block_tip.to_string() << "] "
            << " micro_block_tip: [" << resp.micro_block_tip.to_string() << "] "
            << " epoch_block_seq_number: " << resp.epoch_block_seq_number
            << " micro_block_seq_number: " << resp.micro_block_seq_number
            << "\n";
        for(int i = 0; i < NUMBER_DELEGATES; ++i) {
            out << " batch_block_tip: [" << resp.batch_block_tip[i].to_string() << "] "
                << " batch_block_seq_number: " << resp.batch_block_seq_number[i] << "\n";
        }
        return out;
    }
};

constexpr int tips_response_header_len = 4; /* block_type + pad */
constexpr int tips_response_mesg_len =
    (tips_response_header_len +
     sizeof(uint64_t) + /* timestamp_start */
     sizeof(uint64_t) + /* timestamp_end */
     sizeof(int) + /* delegate_id */
     sizeof(BlockHash) + /* epoch_block_tip */
     sizeof(BlockHash) + /* micro_block_tip */
     (sizeof(BlockHash) * NUMBER_DELEGATES) + /* batch block tips*/
     sizeof(uint32_t) + /* epoch_block_seq_number */
     sizeof(uint32_t) + /* micro_block_seq_number */
     (sizeof(uint32_t) * NUMBER_DELEGATES) /* batch block seq numbers */
    );

/// getBatchBlockTip
/// @param s Store reference
/// @param delegate int
/// @returns BlockHash (the tip we asked for)
BlockHash getBatchBlockTip(Store &s, int delegate);

/// getBatchBlockSeqNr
/// @param s Store reference
/// @param delegate int
/// @returns uint64_t (the sequence number)
uint64_t  getBatchBlockSeqNr(Store &s, int delegate);

} // namespace BatchBlock
