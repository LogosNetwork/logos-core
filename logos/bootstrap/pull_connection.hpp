#pragma once


#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/connection.hpp>
#include <logos/bootstrap/pull.hpp>

namespace Bootstrap
{
    using PullPtr = std::shared_ptr<PullRequest>;

    class bulk_pull_client : public std::enable_shared_from_this<Bootstrap::bulk_pull_client>
    {
    public:
        /// Class constructor
        /// @param bootstrap_client
        /// @param pull_info
        bulk_pull_client (std::shared_ptr<ISocket> connection, Puller & puller);

        /// Class desctructor
        ~bulk_pull_client ();

        /// request_batch_block start of operation
        void run();

        /// receive_block composed operation
        void receive_block ();

//        /// received_type composed operation, receive 1 byte indicating block type
//        void received_type ();
//
//        /// received_block_size composed operation, receive the 4 byte size of the message
//        void received_block_size(boost::system::error_code const &, size_t);
//
//        /// received_block composed operation, receive the actual block
//        void received_block (boost::system::error_code const &, size_t);

        std::shared_ptr<ISocket> connection;
        Puller & puller;
        PullPtr pull;
        Log log;
    };

    class bulk_pull;
    class bulk_pull_server : public std::enable_shared_from_this<Bootstrap::bulk_pull_server>
    {
    public:
        /// Class constructor
        /// @param bootstrap_server
        /// @param bulk_pull (the actual request being made)
        bulk_pull_server (std::shared_ptr<ISocket> server, PullPtr pull);

        ~bulk_pull_server();

        void run();

        void send_block ();
//        /// set_current_end sets end of transmission
//        void set_current_end ();//walk backwards, set start and end
//
//        /// send_next sends next block
//        void send_next ();
//
//        /// sent_action composed operation
//        void sent_action (boost::system::error_code const &, size_t);
//
//        /// send_finished send end of transmission
//        void send_finished ();
//
//        /// no_block_sent
//        void no_block_sent (boost::system::error_code const &, size_t);

        std::shared_ptr<ISocket> connection;
        PullPtr request;
        std::vector<uint8_t> send_buffer;
        Log log;
    };
}



/*
namespace BatchBlock {

const int BULK_PULL_RESPONSE = 65;

struct bulk_pull_response {
    const logos::block_type block_type = logos::block_type::batch_block;
    char pad[3]={0}; // NOTE
    int  block_size; // Size on the wire after serialization.
    int  delegate_id;
    int  retry_count; // Number of times we tried to validate this block.
    int  peer;        // ID of peer who sent us this block.
    std::shared_ptr<ApprovedBSB> block;

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
        block->Serialize(stream, true, true);
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

        MessagePrequel<MessageType::Post_Committed_Block, ConsensusType::BatchStateBlock> prequel(error, stream);
        if(error)
            return error;
        resp.block = std::make_shared<ApprovedBSB>(error, stream, prequel.version, true, true);

        return error; // false == success
    }
};

struct bulk_pull_response_micro {
    const logos::block_type block_type = logos::block_type::micro_block;
    char pad[3]={0}; // NOTE 
    int block_size;
    int delegate_id;
    int retry_count;
    int peer;
    std::shared_ptr<ApprovedMB> micro;

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
        micro->Serialize(stream, true, true);
    }

    /// DeSerialize write out a stream into a bulk_pull_response object
    /// @param stream
    /// @param resp
    static bool DeSerialize(logos::stream & stream, bulk_pull_response_micro & resp)
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

        MessagePrequel<MessageType::Post_Committed_Block, ConsensusType::MicroBlock> prequel(error, stream);
        if(error)
            return error;
        resp.micro = std::make_shared<ApprovedMB>(error, stream, prequel.version, true, true);
        return error; // false == success
    }
};

struct bulk_pull_response_epoch {
    const logos::block_type block_type = logos::block_type::epoch_block;
    char pad[3]={0}; // NOTE 
    int block_size;
    int delegate_id;
    int retry_count;
    int peer;
    std::shared_ptr<ApprovedEB> epoch;

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
        epoch->Serialize(stream, true, true);
    }

    /// DeSerialize write out a stream into a bulk_pull_response object
    /// @param stream
    /// @param resp
    static bool DeSerialize(logos::stream & stream, bulk_pull_response_epoch & resp)
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

        MessagePrequel<MessageType::Post_Committed_Block, ConsensusType::Epoch> prequel(error, stream);
        if(error)
            return error;
        resp.epoch = std::make_shared<ApprovedEB>(error, stream, prequel.version, true, true);

        return error; // false == success
    }
};

constexpr int bulk_pull_response_mesg_len = (sizeof(bulk_pull_response) + sizeof(bulk_pull_response_micro) + sizeof(bulk_pull_response_epoch) + sizeof(ApprovedBSB) + sizeof(ApprovedMB) + sizeof(ApprovedEB));

/// Validate wrapper to call BSB Validation methods for a BSB block
/// @param store BlockStore
/// @param message ApprovedBSB
/// @param delegate_id
/// @param status the ValidationStatus of the check
/// @returns boolean (true if validation succeeded)
bool Validate(Store & store, const ApprovedBSB & message, int delegate_id, ValidationStatus * status);

/// ApplyUpdates wrapper to write into the database after successful validation
/// @param store BlockStore
/// @param message ApprovedBSB
/// @param delegate_id
void ApplyUpdates(Store & store, const ApprovedBSB & message, uint8_t delegate_id);

/// getNextBatchStateBlock return the next BSB block in the chain given current block
/// @param store BlockStore
/// @param delegate
/// @param h BlockHash
/// @returns BlockHash of next block
BlockHash getNextBatchStateBlock(Store &store, int delegate, BlockHash &h);

/// readBatchStateBlock get the data associated with the BlockHash
/// @param store BlockStore
/// @param h hash of the block we are to read
/// @returns shared pointer of ApprovedBSB
std::shared_ptr<ApprovedBSB> readBatchStateBlock(Store &store, BlockHash &h);

/// getPrevBatchStateBlock return the previous BSB block in the chain given current block
/// @param store BlockStore
/// @param delegate
/// @param h BlockHash
/// @returns BlockHash of previous block
BlockHash getPrevBatchStateBlock(Store &store, int delegate, BlockHash &h);

} // BatchBlock
*/
