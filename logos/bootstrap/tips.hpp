#pragma once

#include <logos/bootstrap/bootstrap_messages.hpp>
//#include <logos/bootstrap/bootstrap.hpp>
#include <logos/lib/log.hpp>

namespace Bootstrap
{
    class TipUtils{
    public:
//        static TipSet CreateTipSet(Store & store)
//        {
//            //logos::transaction transaction (store.environment, nullptr, false);
//
//            return TipSet();
//        }

	//        /*
	//         * We assume both a and b are valid tips, in this iteration of the bootstrapping.
	//         * One of the TODOs in the next release of bootstrapping is to validate the peer's tips.
	//         * We could ask the peer for all the approved blocks included in the tips.
	//         * If all the blocks have valid agg-signature, then we consider the peer's tips are valid.
	//         */
	//        static bool IsBehind(const TipSet & a, const TipSet & b)
	//        {
	//        	if(a.eb > b.eb)
	//        		return false;
	//        	else if(b.eb > a.eb)
	//        		return true;
	//        	else{
	//            	if(a.mb > b.mb)
	//            		return false;
	//            	else if(b.mb > a.mb)
	//            		return true;
	//            	else{
	//            		/*
	//            		 * At this point, a and b have same eb and mb.
	//            		 * We consider that a IsBehind b if any of the 2 cases are true:
	//            		 * (1) if a is behind b on any of the 32 batch chains.
	//            		 * (2) if a is behind b on any of the 32 batch chains, in the new epoch
	//            		 */
	//                    for(uint i = 0; i < NUM_DELEGATES; ++i)
	//                    {
	//                        if(a.bsb_vec[i] < b.bsb_vec[i])
	//                        	return true;
	//                    }
	//                    for(uint i = 0; i < NUM_DELEGATES; ++i)
	//                    {
	//                        if(a.bsb_vec_new_epoch[i] < b.bsb_vec_new_epoch[i])
	//                        	return true;
	//                    }
	//                    return false;
	//            	}
	//        	}
	//        }
    };
}
//					/*
//					 * (2) if a doesn't have tips from new epoch, but b has.
//					 */
//                    if(a.bsb_vec_new_epoch.size()==0 && b.bsb_vec_new_epoch.size()==NUM_DELEGATES)
//                    	return true;
//                    /*
//                     * In the case that a has tips from new epoch, but b doesn't have, and we already checked
//                     * the 3 cases above, a cannot be behind b.
//                     */
