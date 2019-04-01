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

        /// ostream operator
        /// @param out ostream reference
        /// @param resp BatchBlock::tips_response (object to stream out)
        /// @returns ostream operator
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
    };
}
