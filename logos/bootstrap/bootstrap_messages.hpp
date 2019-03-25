#pragma once
#include <boost/iostreams/stream_buffer.hpp>

#include <logos/blockstore.hpp>
#include <logos/bootstrap/tips.hpp>
#include <logos/consensus/messages/messages.hpp>

namespace Bootstrap {

    using Store = logos::block_store;

    constexpr uint32_t BootstrapBufExtra = 1024;
    constexpr uint32_t BootstrapBufSize = MAX_MSG_SIZE + BootstrapBufExtra;

    enum class MessageType : uint8_t
    {
        TipRequest   = 0,
        TipResponse  = 1,
        PullRequest  = 2,
        PullResponse = 3,

		Unknown      = 0xff
    };

    struct MessageHeader
    {
        MessageHeader() = default;
        MessageHeader(uint8_t version,
        		MessageType type,
				ConsensusType ct,
				uint32_t payload_size);
        MessageHeader(bool & error, logos::stream & stream);
        uint32_t Serialize(logos::stream & stream) const;

        bool Validate();

        uint8_t version = logos_version;
        MessageType type = MessageType::Unknown;
        ConsensusType pull_response_ct = ConsensusType::Any;
        uint8_t mpf = 0;
        mutable uint32_t payload_size = 0;

        static constexpr uint32_t WireSize = 8;
    };

    struct PullRequest {
        ConsensusType block_type;
        uint32_t epoch_num;
        BlockHash prev_hash;
        BlockHash target; //ignored for EB and MB since we pull one at a time

        PullRequest(ConsensusType block_type,
        	uint32_t epoch_num,
            const BlockHash &prev,
			const BlockHash &target=0);

        PullRequest(bool & error, logos::stream & stream);

        uint32_t Serialize(logos::stream & stream) const;
        bool operator==(const PullRequest & other) const;

        static constexpr uint32_t WireSize = sizeof(block_type) +
        		sizeof(epoch_num) + HASH_SIZE * 2;
    };


    enum class PullResponseStatus : uint8_t
    {
        MoreBlock,
        LastBlock,
        NoBlock
    };

    template<ConsensusType CT>
    struct PullResponse
    {
        PullResponseStatus status = PullResponseStatus::NoBlock;
        shared_ptr<PostCommittedBlock<CT>> block;

        PullResponse()= default;
        PullResponse(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, status);
            if(error)
            {
                return;
            }
            if(status == PullResponseStatus::NoBlock)
            {
            	block = nullptr;
                return;
            }

            Prequel prequel(error, stream);
            if (error)
            {
                return;
            }
            block = std::make_shared<PostCommittedBlock<CT>>(error,
            		stream,
					prequel.version,
					true,
					true);
        }

        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = logos::write(stream, status);
            if(status!=PullResponseStatus::NoBlock)
            	s += block->Serialize(stream, true, true);
            return s;
        }

        bool operator==(const PullResponse & other) const
		{
        	if(status!=other.status)
        		return false;

        	if(status!=PullResponseStatus::NoBlock)
        	{
        		return block->Hash() == other.block->Hash();
        	}
        	return true;
		}
    };

    /**
     * At the server side, to save a round of deserialization and then serialization,
     * we serialize the meta-data fields and memcpy the block directly to the buffer.
     * When this function is call, buf should already have the block
     */
    constexpr uint32_t PullResponseReserveSize =
    		MessageHeader::WireSize + sizeof(PullResponseStatus);
    //return total message size including header
    uint32_t PullResponseSerializedLeadingFields(ConsensusType ct,
            PullResponseStatus status,
            uint32_t block_size,
            std::vector<uint8_t> & buf);

#define BOOTSTRAP_PROGRESS
#ifdef BOOTSTRAP_PROGRESS
#include <atomic>
    unsigned get_block_progress();
	void block_progressed();
#endif

} // namespace



/*
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
*/
