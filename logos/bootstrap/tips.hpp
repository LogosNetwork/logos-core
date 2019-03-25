#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/tip.hpp>
#include <logos/lib/log.hpp>

namespace Bootstrap
{
	using Store = logos::block_store;

    struct TipSet
    {
        Tip eb;
        Tip mb;
        std::array<Tip, NUM_DELEGATES> bsb_vec;
        std::array<Tip, NUM_DELEGATES> bsb_vec_new_epoch;
        //TODO Recall: in case of recall before epoch block, we could have more than two sets of tips

        /// Class constructor
        TipSet() = default;
        TipSet(bool & error, logos::stream & stream);

        /// Serialize
        /// write this object out as a stream.
        uint32_t Serialize(logos::stream & stream) const;

        bool operator==(const TipSet & other) const;

        static constexpr uint32_t WireSize = Tip::WireSize * (1 + 1 + NUM_DELEGATES * 2);

        /*
         * We assume both a and b are valid tips, in this iteration of the bootstrapping.
         * One of the TODOs in the next release of bootstrapping is to validate the peer's tips.
         * We could ask the peer for all the approved blocks included in the tips.
         * If all the blocks have valid agg-signature, then we consider the peer's tips are valid.
         */
        bool IsBehind(const TipSet & other);

        uint32_t GetLatestEpochNumber();
        static TipSet CreateTipSet(Store & store);
    };
}

//
//	//        /*
//	//         * We assume both a and b are valid tips, in this iteration of the bootstrapping.
//	//         * One of the TODOs in the next release of bootstrapping is to validate the peer's tips.
//	//         * We could ask the peer for all the approved blocks included in the tips.
//	//         * If all the blocks have valid agg-signature, then we consider the peer's tips are valid.
//	//         */
//	//        static bool IsBehind(const TipSet & a, const TipSet & b)
//	//        {
//	//        	if(a.eb > b.eb)
//	//        		return false;
//	//        	else if(b.eb > a.eb)
//	//        		return true;
//	//        	else{
//	//            	if(a.mb > b.mb)
//	//            		return false;
//	//            	else if(b.mb > a.mb)
//	//            		return true;
//	//            	else{
//	//            		/*
//	//            		 * At this point, a and b have same eb and mb.
//	//            		 * We consider that a IsBehind b if any of the 2 cases are true:
//	//            		 * (1) if a is behind b on any of the 32 batch chains.
//	//            		 * (2) if a is behind b on any of the 32 batch chains, in the new epoch
//	//            		 */
//	//                    for(uint i = 0; i < NUM_DELEGATES; ++i)
//	//                    {
//	//                        if(a.bsb_vec[i] < b.bsb_vec[i])
//	//                        	return true;
//	//                    }
//	//                    for(uint i = 0; i < NUM_DELEGATES; ++i)
//	//                    {
//	//                        if(a.bsb_vec_new_epoch[i] < b.bsb_vec_new_epoch[i])
//	//                        	return true;
//	//                    }
//	//                    return false;
//	//            	}
//	//        	}
//	//        }
//    };
//}
////					/*
////					 * (2) if a doesn't have tips from new epoch, but b has.
////					 */
////                    if(a.bsb_vec_new_epoch.size()==0 && b.bsb_vec_new_epoch.size()==NUM_DELEGATES)
////                    	return true;
////                    /*
////                     * In the case that a has tips from new epoch, but b doesn't have, and we already checked
////                     * the 3 cases above, a cannot be behind b.
////                     */
//BlockHash getBatchBlockTip(Store & store, int delegate)
//{
//    BlockHash hash = 0;
//    if(!store.batch_tip_get(delegate, hash)) {
//        return hash;
//    }
//    return hash;
//}
//
//uint32_t  getBatchBlockTipSeqNr(Store& store, uint8_t delegate)
//{
//    // The below code will dump core in bsb dtor.
//    // Also, the sequence number is not correct. Need real world database to test.
//    ApprovedBSB batch;
//    BlockHash hash = getBatchBlockTip(store,delegate);
//    if(hash.is_zero())
//    {
//        return 0;
//    }
//    if(store.batch_block_get(hash, batch))
//    {
//		trace_and_halt();
//    }
//    return batch.sequence;
//}
//
//BlockHash getMicroBlockTip(Store& s)
//{
//    BlockHash hash;
//    if(!s.micro_block_tip_get(hash)) {
//        return hash;
//    }
//    return BlockHash();
//}
//std::shared_ptr<ApprovedMB> readMicroBlock(Store &store, BlockHash &hash)
//{
//    std::shared_ptr<ApprovedMB> micro(new ApprovedMB);
//    if(!store.micro_block_get(hash,*micro)) {
//        return micro;
//    }
//    return nullptr;
//}
//uint32_t  getMicroBlockTipSeqNr(Store& s)
//{
//    BlockHash hash = getMicroBlockTip(s);
//    std::shared_ptr<ApprovedMB> tip = readMicroBlock(s,hash);
//    return tip->sequence;
//}
//
//BlockHash getEpochBlockTip(Store& s)
//{
//    BlockHash hash;
//    if(!s.epoch_tip_get(hash)) {
//        return hash;
//    }
//    return BlockHash();
//}
//std::shared_ptr<ApprovedEB> readEpochBlock(Store &store, BlockHash &hash)
//{
//    std::shared_ptr<ApprovedEB> epoch(new ApprovedEB);
//    if(!store.epoch_get(hash,*epoch)) {
//        return epoch;
//    }
//    return nullptr;
//}
//uint64_t  getEpochBlockSeqNr(Store& s)
//{
//    BlockHash hash = getEpochBlockTip(s);
//    std::shared_ptr<ApprovedEB> tip = readEpochBlock(s,hash);
//    return tip->epoch_number;
//}
//};//	class TipUtils
//	{
//	public:
//    	static
