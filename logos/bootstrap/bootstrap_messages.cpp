#include <logos/bootstrap/bootstrap_messages.hpp>

namespace Bootstrap
{

    MessageHeader::MessageHeader(uint8_t version, MessageType type, ConsensusType ct, uint32_t payload_size)
    : version(version)
    , type(type)
    , pull_response_ct(ct)
    , payload_size(payload_size)
    {}

    MessageHeader::MessageHeader(bool & error, logos::stream & stream)
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

    uint32_t MessageHeader::Serialize(logos::stream & stream) const
    {
        auto s = logos::write(stream, version);
        s += logos::write(stream, type);
        s += logos::write(stream, pull_response_ct);
        s += logos::write(stream, mpf);
        s += logos::write(stream, payload_size);

        assert(s == WireSize);
        return s;
    }

    bool MessageHeader::Validate()
    {
        return version != logos_version &&
               (type == MessageType::TipRequest ||
                type == MessageType::PullRequest ||
                type == MessageType::TipResponse ||
                type == MessageType::PullResponse
                ) &&
               payload_size <= BootstrapBufSize - WireSize;
    }

    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////

    Tip::Tip()
    : epoch(0)
    , sqn(0)
    , digest(0)
    {}

    Tip::Tip(uint32_t epoch, uint32_t sqn, const BlockHash & digest)
    : epoch(epoch)
    , sqn(sqn)
    , digest(digest)
    {}

    Tip::Tip(bool & error, logos::stream & stream)
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

    uint32_t Tip::Serialize(logos::stream & stream) const
    {
        auto s = logos::write(stream, epoch);
        s += logos::write(stream, sqn);
        s += logos::write(stream, digest);

        assert(s == WireSize);
        return s;
    }

    bool Tip::operator<(const Tip & other) const
	{
    	return epoch < other.epoch ||
    			(epoch == other.epoch && sqn < other.sqn) ||
				(epoch == other.epoch && sqn==0 && other.sqn==0 && digest==0 && other.digest!=0);
	}

    bool Tip::operator==(const Tip & other) const
	{
    	return epoch==other.epoch && sqn==other.sqn && digest==other.digest;
	}

    ////////////////////////////////////////////////////////////////////

    TipSet::TipSet(bool & error, logos::stream & stream)
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
    uint32_t TipSet::Serialize(logos::stream & stream) const
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

    bool TipSet::operator==(const TipSet & other) const
	{
    	if(eb==other.eb)
    	{
    		if(mb==other.mb)
    		{
    	        for(uint i = 0; i < NUM_DELEGATES; ++i)
    	        {
    	            if(! (bsb_vec[i]==other.bsb_vec[i]))
    	            	return false;
    	        }

    	        for(uint i = 0; i < NUM_DELEGATES; ++i)
    	        {
    	        	if(! (bsb_vec_new_epoch[i]==other.bsb_vec_new_epoch[i]))
    	        		return false;
    	        }
    	        return true;
    		}
    	}
    	return false;
	}

    TipSet TipSet::CreateTipSet(Store & store)
	{
    	//TODO
		return TipSet();
	}

    /*
     * We assume both a and b are valid tips, in this iteration of the bootstrapping.
     * One of the TODOs in the next release of bootstrapping is to validate the peer's tips.
     * We could ask the peer for all the approved blocks included in the tips.
     * If all the blocks have valid agg-signature, then we consider the peer's tips are valid.
     */
    bool TipSet::IsBehind(const TipSet & other)
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

    uint32_t TipSet::GetLatestEpochNumber()
    {
        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
        	if(!bsb_vec_new_epoch[i].digest.is_zero())
        	{
        		return bsb_vec_new_epoch[i].epoch;
        	}
        }

        return bsb_vec[0].epoch;
    }

    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////


    PullRequest::PullRequest(ConsensusType block_type,
    	//uint32_t epoch_num,
        const BlockHash &prev,
		const BlockHash &target)
    : block_type(block_type)
    //, epoch_num(epoch_num)
    , prev_hash(prev)
    , target(target)
    { }

    PullRequest::PullRequest(bool & error, logos::stream & stream)
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

    uint32_t PullRequest::Serialize(logos::stream & stream) const
    {
        auto s = logos::write(stream, block_type);
        //s += logos::write(stream, epoch_num);
        s += logos::write(stream, prev_hash);
        s += logos::write(stream, target);

        assert(s == WireSize);
        return s;
    }

    bool PullRequest::operator==(const PullRequest & other) const{
    	return block_type==other.block_type && prev_hash==other.prev_hash &&
    			target==other.target;
    }

    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////
//
//    template<ConsensusType CT>
//    PullResponse<CT>::PullResponse(bool & error, logos::stream & stream)
//    {
//        error = logos::read(stream, status);
//        if(error)
//        {
//            return;
//        }
//        if(status == PullResponseStatus::NoBlock)
//        {
//        	block = nullptr;
//            return;
//        }
//
//        Prequel prequel(error, stream);
//        if (error)
//        {
//            return;
//        }
//        block = std::make_shared<PostCommittedBlock<CT>>(error, stream, prequel.version, true, true);
//    }

	using LeadingFieldsStream =
			boost::iostreams::stream_buffer<boost::iostreams::basic_array_sink<uint8_t>>;

	//return total message size including header
	uint32_t PullResponseSerializedLeadingFields(ConsensusType ct,
			PullResponseStatus status,
			uint32_t block_size,
			std::vector<uint8_t> & buf)
	{
		LeadingFieldsStream stream(buf.data(), PullResponseReserveSize);
		uint32_t payload_size = sizeof(status) + block_size;
		MessageHeader header(logos_version,
				MessageType::PullResponse,
				ct,
				payload_size);
		header.Serialize(stream);
		logos::write(stream, status);
		return MessageHeader::WireSize + payload_size;
	}

#ifdef BOOTSTRAP_PROGRESS
	std::atomic<unsigned> num_blocks_processed(0);
	unsigned get_block_progress_and_reset()
	{
		unsigned b = num_blocks_processed;
		num_blocks_processed = 0;
		return b;
	}
	void block_progressed()
	{
		num_blocks_processed++;
	}
#endif
}
