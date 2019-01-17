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
    int  block_size; // Size on the wire after serialization.
    int  delegate_id;
    int  retry_count; // Number of times we tried to validate this block.
    int  peer;        // ID of peer who sent us this block.
    BatchStateBlock block;

    /// Serialize write out the bulk_pull_response into a vector<uint8_t>
    /// @param stream
    void Serialize(logos::stream & stream)
    {
        logos::write(stream, block_type);
        logos::write(stream, pad[0]);
        logos::write(stream, pad[1]);
        logos::write(stream, pad[2]);
        logos::write(stream, block_size);
        logos::write(stream, delegate_id);
        logos::write(stream, retry_count);
        logos::write(stream, peer);
        block.Serialize(stream);
    }

    /// DeSerialize write out a stream into a bulk_pull_response object
    /// @param stream
    /// @param resp
    static bool DeSerialize(logos::stream & stream, bulk_pull_response & resp)
    {
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
        error = logos::read(stream, resp.block_size);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.delegate_id);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.retry_count);
        if(error) {
            return error;
        }
        error = logos::read(stream, resp.peer);
        if(error) {
            return error;
        }
        resp.block = BatchStateBlock(error, stream);
        return error; // false == success
    }
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

/// Validate wrapper to call BSB Validation methods for a BSB block
/// @param store BlockStore
/// @param message BatchStateBlock
/// @param delegate_id
/// @returns boolean (true if validation succeeded)
bool Validate(Store & store, const BatchStateBlock & message, int delegate_id);

/// ApplyUpdates wrapper to write into the database after successful validation
/// @param store BlockStore
/// @param message BatchStateBlock
/// @param delegate_id
void ApplyUpdates(Store & store, const BatchStateBlock & message, uint8_t delegate_id);

/// getNextBatchStateBlock return the next BSB block in the chain given current block
/// @param store BlockStore
/// @param delegate
/// @param h BlockHash
/// @returns BlockHash of next block
BlockHash getNextBatchStateBlock(Store &store, int delegate, BlockHash &h);

/// readBatchStateBlock get the data associated with the BlockHash
/// @param store BlockStore
/// @param h hash of the block we are to read
/// @returns shared pointer of BatchStateBlock
std::shared_ptr<BatchStateBlock> readBatchStateBlock(Store &store, BlockHash &h);

/// getPrevBatchStateBlock return the previous BSB block in the chain given current block
/// @param store BlockStore
/// @param delegate
/// @param h BlockHash
/// @returns BlockHash of previous block
BlockHash getPrevBatchStateBlock(Store &store, int delegate, BlockHash &h);

} // BatchBlock
