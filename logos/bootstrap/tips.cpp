#include <logos/blockstore.hpp>
#include <logos/bootstrap/tips.hpp>

namespace Bootstrap
{
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

	////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////


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

	TipSet TipSet::CreateTipSet(Store & store)
	{
		/*
		 * assuming epoch tip and micro tip always exist, due to genesis blocks
		 * 1) get epoch tip
		 * 2) get micro tip
		 * 3) get e#_e in epoch and e#_m in micro, if same goto 4), else 5)
		 * 4) (1st micro of e#+1 not stored)
		 *    use e#_m+1 and [0,31] to get 32 bsb tips and put in bsb_vec
		 *    zero out bsb_vec_new_epoch
		 *    note that we always have 32 tips in this case because
		 *    	from Mar 19, 2019 discussion:
		 *    	(i) on network launch, 32 empty bsbs by 32 delegates are created
		 *    	TODO see if can get rid of (i)
		 *    	(ii) old tips are carried over if nothing created in new epoch
		 * 5) assert e#_e+1==e#_m
		 * 	  if micro is the last of an epoch, goto 6), else 7)
		 * 6) (unlikely, but could happen. last micro stored, but no epoch)
		 * 	  use e#_m and [0,31] to get 32 bsb tips and put in bsb_vec
		 * 	  use e#_m+1 and [0,31] to get 32 bsb tips and put in
		 * 	      bsb_vec_new_epoch, if not available, zero out that slot
		 * 7) same as 6)
		 *
		 * so we only have two cases:
		 * if e#_e==e#_m, goto 4), else 6)
		 */

		TipSet tips;
		Log log;
		memset(&tips, 0, sizeof(tips));
		logos::transaction transaction (store.environment, nullptr, false);

		if(store.epoch_tip_get(tips.eb, transaction))
		{
			LOG_FATAL(log) << "TipSet::CreateTipSet cannot get epoch tip";
			trace_and_halt();
		}
		if(store.micro_block_tip_get(tips.mb, transaction))
		{
			LOG_FATAL(log) << "TipSet::CreateTipSet cannot get micro tip";
			trace_and_halt();
		}

		//assert(tips.eb.epoch==tips.mb.epoch || tips.eb.epoch+1==tips.mb.epoch);
		if(tips.eb.epoch==tips.mb.epoch)
		{
			for(uint8_t i=0; i < NUM_DELEGATES; ++i)
			{
				store.request_tip_get(i, tips.mb.epoch+1, tips.bsb_vec[i], transaction);
			}
		}
		else if(tips.eb.epoch+1==tips.mb.epoch)
		{
			for(uint8_t i=0; i < NUM_DELEGATES; ++i)
			{
				store.request_tip_get(i, tips.mb.epoch, tips.bsb_vec[i], transaction);
				store.request_tip_get(i, tips.mb.epoch+1, tips.bsb_vec_new_epoch[i], transaction);
			}
		}
		else
		{
			LOG_FATAL(log) << "TipSet::CreateTipSet "
					<<"tips.eb.epoch!=tips.mb.epoch && tips.eb.epoch+1!=tips.mb.epoch";
			trace_and_halt();
		}

		return tips;
	}
}
