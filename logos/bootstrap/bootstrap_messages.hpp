#pragma once
#include <boost/iostreams/stream_buffer.hpp>

#include <logos/blockstore.hpp>
#include <logos/consensus/messages/messages.hpp>

namespace Bootstrap {

    using Store = logos::block_store;

    constexpr uint32_t BootstrapBufExtra = 4096;
    //One page more than max consensus message size
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

        MessageHeader(uint8_t version, MessageType type, ConsensusType ct, uint32_t payload_size)
        : version(version)
        , type(type)
        , pull_response_ct(ct)
        , payload_size(payload_size)
        {}

        MessageHeader(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, version);
            if(error)
            {
                return;
            }
            error = logos::read(stream, type);
            if(error)
            {
                return;
            }
            error = logos::read(stream, pull_response_ct);
            if(error)
            {
                return;
            }
            error = logos::read(stream, mpf);
            if(error)
            {
                return;
            }
            error = logos::read(stream, payload_size);
            if(error)
            {
                return;
            }
        }

        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = logos::write(stream, version);
            s += logos::write(stream, type);
            s += logos::write(stream, pull_response_ct);
            s += logos::write(stream, mpf);
            s += logos::write(stream, payload_size);

            assert(s == WireSize);
            return s;
        }

        bool Validate()
        {
            return version != logos_version &&
                   (type == MessageType::TipRequest ||
                    type == MessageType::PullRequest ||
                    type == MessageType::TipResponse ||
                    type == MessageType::PullResponse
                    ) &&
                   payload_size <= BootstrapBufSize - WireSize;
        }

        uint8_t version = logos_version;
        MessageType type = MessageType::Unknown;
        ConsensusType pull_response_ct = ConsensusType::Any;
        uint8_t mpf = 0;
        mutable uint32_t payload_size = 0;

        static constexpr uint32_t WireSize = 8;
    };

    struct Tip
    {
        uint32_t epoch;
        uint32_t sqn; //same as epoch for epoch_blocks
        BlockHash digest;

        Tip()
        : epoch(0)
        , sqn(0)
        , digest(0)
        {}

        Tip(uint32_t epoch, uint32_t sqn, const BlockHash & digest)
        : epoch(epoch)
        , sqn(sqn)
        , digest(digest)
        {}

        Tip(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, epoch);
            if(error)
            {
                return;
            }
            error = logos::read(stream, sqn);
            if(error)
            {
                return;
            }
            error = logos::read(stream, digest);
        }

        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = logos::write(stream, epoch);
            s += logos::write(stream, sqn);
            s += logos::write(stream, digest);

            assert(s == WireSize);
            return s;
        }

        bool operator<(const Tip & other) const
		{
        	return epoch < other.epoch ||
        			(epoch == other.epoch && sqn < other.sqn) ||
					(epoch == other.epoch && sqn==0 && other.sqn==0 && digest==0 && other.digest!=0);
		}

        static constexpr uint32_t WireSize = sizeof(epoch) + sizeof(sqn) + HASH_SIZE;
    };

    struct TipSet
    {
        Tip eb;
        Tip mb;
        std::array<Tip, NUM_DELEGATES> bsb_vec;
        std::array<Tip, NUM_DELEGATES> bsb_vec_new_epoch;

        /// Class constructor
        TipSet() = default;
        TipSet(bool & error, logos::stream & stream)
        {
            new(&eb) Tip(error, stream);
            if (error) {
                return;
            }
            new(&mb) Tip(error, stream);
            if (error) {
                return;
            }
            for (uint8_t i = 0; i < NUM_DELEGATES; ++i) {
                bsb_vec[i] = Tip(error, stream);
                if (error) {
                    return;
                }
            }

            for (uint8_t i = 0; i < NUM_DELEGATES; ++i)
			{
				bsb_vec_new_epoch[i] = Tip(error, stream);
				if (error) {
					return;
				}
			}
        }

        /// Serialize
        /// write this object out as a stream.
        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = eb.Serialize(stream);
            s += mb.Serialize(stream);

            for(uint i = 0; i < NUM_DELEGATES; ++i)
            {
                s += bsb_vec[i].Serialize(stream);
            }

            for(uint i = 0; i < NUM_DELEGATES; ++i)
            {
            	s += bsb_vec_new_epoch[i].Serialize(stream);
            }

            assert(s == WireSize);
            return s;
        }

        static constexpr uint32_t WireSize = Tip::WireSize * (1 + 1 + NUM_DELEGATES * 2);

        static TipSet CreateTipSet(Store & store)
		{
			//logos::transaction transaction (store.environment, nullptr, false);
			//            BlockHash epoch_tip = getEpochBlockTip(store);
			//            BlockHash micro_tip = getMicroBlockTip(store);
			//            uint32_t  epoch_seq = getEpochBlockSeqNr(store);
			//            uint32_t  micro_seq = getMicroBlockSeqNr(store);
			//            epoch_block_tip        = epoch_tip;
			//            micro_block_tip        = micro_tip;
			//            epoch_block_seq_number = epoch_seq;
			//            micro_block_seq_number = micro_seq;
			//
			//            Micro::dumpMicroBlockTips(store,micro_tip);
			//
			//            // NOTE Get the tips for all delegates and send them one by one for processing...
			//            for(int i = 0; i < NUMBER_DELEGATES; i++)
			//            {
			//                BlockHash bsb_tip   = getBatchBlockTip(store, i);
			//                uint32_t  bsb_seq   = getBatchBlockSeqNr(store, i);
			//                // Fill in the response...
			//                batch_block_tip[i]        = bsb_tip;
			//                batch_block_seq_number[i] = bsb_seq;
			//            }
			return TipSet();
		}

        /*
         * We assume both a and b are valid tips, in this iteration of the bootstrapping.
         * One of the TODOs in the next release of bootstrapping is to validate the peer's tips.
         * We could ask the peer for all the approved blocks included in the tips.
         * If all the blocks have valid agg-signature, then we consider the peer's tips are valid.
         */
        bool IsBehind(const TipSet & other)
		{
			if(eb < other.eb)
				return true;
			else if(other.eb < eb)
				return false;
			else{
				if(mb < other.mb)
					return true;
				else if(other.mb < mb)
					return false;
				else{
					/*
					 * At this point, a and other have same eb and mb.
					 * We consider that a IsBehind other if any of the 2 cases are true:
					 * (1) if a is behind other on any of the 32 batch chains.
					 * (2) if a is behind other on any of the 32 batch chains, in the new epoch
					 */
					for(uint i = 0; i < NUM_DELEGATES; ++i)
					{
						if(bsb_vec[i] < other.bsb_vec[i])
							return true;
					}
					for(uint i = 0; i < NUM_DELEGATES; ++i)
					{
						if(bsb_vec_new_epoch[i] < other.bsb_vec_new_epoch[i])
							return true;
					}
					return false;
				}
			}
		}
    };

    struct PullRequest {
        ConsensusType block_type;
        //uint32_t epoch_num;
        BlockHash prev_hash;
        BlockHash target; //ignored for EB and MB since we pull one at a time

        PullRequest(ConsensusType block_type,
        	//uint32_t epoch_num,
            const BlockHash &prev,
			const BlockHash &target=0)
        : block_type(block_type)
        //, epoch_num(epoch_num)
        , prev_hash(prev)
        , target(target)
        { }

        PullRequest(bool & error, logos::stream & stream)
        {
            error = logos::read(stream, block_type);
            if(error)
            {
                return;
            }
//            error = logos::read(stream, epoch_num);
//            if(error)
//            {
//                return;
//            }
            error = logos::read(stream, prev_hash);
            if(error)
            {
                return;
            }
            error = logos::read(stream, target);
        }

        uint32_t Serialize(logos::stream & stream) const
        {
            auto s = logos::write(stream, block_type);
            //s += logos::write(stream, epoch_num);
            s += logos::write(stream, prev_hash);
            s += logos::write(stream, target);

            assert(s == WireSize);
            return s;
        }

        static constexpr uint32_t WireSize = sizeof(block_type) + HASH_SIZE * 2;
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
        PullResponseStatus status;
        shared_ptr<PostCommittedBlock<CT>> block;

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
            block = std::make_shared<PostCommittedBlock<CT>>(error, stream, true, true);
        }
    };

    /**
     * At the server side, to save a round of deserialization and then serialization,
     * we serialize the meta-data fields and memcpy the block directly to the buffer.
     * When this function is call, buf should already have the block
     */
    constexpr uint32_t PullResponseReserveSize = MessageHeader::WireSize + sizeof(PullResponseStatus);
    using LeadingFieldsStream =
    		boost::iostreams::stream_buffer<boost::iostreams::basic_array_sink<uint8_t>>;
    //return total message size including header
    uint32_t PullResponseSerializedLeadingFields(ConsensusType ct,
            PullResponseStatus status,
            uint32_t block_size,
            std::vector<uint8_t> & buf)
    {
        //uint32_t serial_size = MessageHeader::WireSize + sizeof(status);
        LeadingFieldsStream stream(buf, PullResponseReserveSize);
        uint32_t payload_size = sizeof(status) + block_size;
        MessageHeader header(logos_version,
        		MessageType::PullResponse,
				ct,
				payload_size);
        header.Serialize(stream);
        logos::write(stream, status);
        return MessageHeader::WireSize + payload_size;
    }

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
