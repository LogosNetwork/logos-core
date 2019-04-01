#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/tip.hpp>
#include <logos/lib/log.hpp>

namespace Bootstrap
{
	using Store = logos::block_store;

    struct TipSet
    {
    	/**
    	 * constructor
    	 */
		TipSet() = default;

        /**
         * constructor from parsing data from a stream
         * @param error out parameter, set if cannot parse
         * @param stream the stream containing the tipset raw data
         */
        TipSet(bool & error, logos::stream & stream);

        /**
         * write this object out to a stream.
         * @param stream the output stream
         * @return number of bytes streamed
         */
        uint32_t Serialize(logos::stream & stream) const;

        /**
         * If the tipset equals to the other tipset
         * @param other the other tipset
         * @return true if equal
         */
        bool operator==(const TipSet & other) const;

		/**
		 * If the tipset is behind the other tipset. A tipset is behind if its epoch tip,
		 * or the micro block tip, or any of the request block tips is behind.
		 *
		 * @param other the other tipset
		 * @return true if behind
		 */
        bool IsBehind(const TipSet & other);
        /*
         * TODO
         * We assume both a and b are valid tips, in this iteration of the bootstrapping.
         * One of the TODOs in the next release of bootstrapping is to validate the peer's tips.
         * We could ask the peer for all the approved blocks included in the tips.
         * If all the blocks have valid agg-signature, then we consider the peer's tips are valid.
         */

        /**
         * get the largest epoch number of the tips in this tipset
         * @return the largest epoch number
         */
        uint32_t GetLatestEpochNumber();

        /**
         * create a set of tips of the local node
         * @param store the database
         * @return the tipset
         */
        static TipSet CreateTipSet(Store & store);

        /**
         * ostream operator
         * @param out ostream reference
         * @param tips object to stream out
         * @return ostream operator
         */
        friend
        ostream& operator<<(ostream &out, const TipSet & tips)
        {
            out << " epoch_block_tip: " << tips.eb.to_string() << "\n";
            out << " micro_block_tip: " << tips.mb.to_string() << "\n";

            for(int i = 0; i < NUM_DELEGATES; ++i) {
                out << " batch_block_tip:     " << tips.bsb_vec[i].to_string() << "\n";
            }
            for(int i = 0; i < NUM_DELEGATES; ++i) {
                out << " batch_block_tip_new: " << tips.bsb_vec_new_epoch[i].to_string() << "\n";
            }
            return out;
        }


        Tip eb;
        Tip mb;
        std::array<Tip, NUM_DELEGATES> bsb_vec;
        std::array<Tip, NUM_DELEGATES> bsb_vec_new_epoch;
        //TODO Recall: in case of recall before epoch block, we could have more than two sets of tips

        static constexpr uint32_t WireSize = Tip::WireSize * (1 + 1 + NUM_DELEGATES * 2);
    };
}
