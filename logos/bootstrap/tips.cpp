#include <logos/bootstrap/tips.hpp>

namespace Bootstrap
{
	TipSet CreateTipSet(Store & store)
	{
		//logos::transaction transaction (store.environment, nullptr, false);
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
}
