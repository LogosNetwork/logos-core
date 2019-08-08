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

        TipSet & operator=(const TipSet & other)
        {
            // check for self-assignment
            if(&other == this)
                return *this;
            this->eb = other.eb;
            this->mb = other.mb;
            this->bsb_vec = other.bsb_vec;
            this->bsb_vec_new_epoch = other.bsb_vec_new_epoch;
            this->eb_tip_total_RBs = other.eb_tip_total_RBs;
            return *this;
        }

        /**
         * If the tipset is behind the other tipset. A tipset is behind if its epoch tip,
         * or the micro block tip, or any of the request block tips is behind.
         *
         * @param other the other tipset
         * @return true if behind
         */
        bool IsBehind(const TipSet & other) const;

        bool ValidTips() const;
        bool ValidPeerTips(const TipSet & others) const;

        uint64_t ComputeNumberAllRBs() const;

        void ComputeNumberBlocksBehind(const TipSet & other, uint32_t & num_eb, uint32_t & num_mb, uint64_t & num_rb) const;

        /**
         * get the largest epoch number of the tips in this tipset
         * @return the largest epoch number
         */
        uint32_t GetLatestEpochNumber() const;

        /**
         * create a set of tips of the local node
         * @param store the database
         * @return the tipset
         */
        static TipSet CreateTipSet(Store & store, bool write_transaction=false);

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

            out << " RBs till epoch_block_tip: " << tips.eb_tip_total_RBs << "\n";
            return out;
        }


        Tip eb;
        Tip mb;
        std::array<Tip, NUM_DELEGATES> bsb_vec;
        std::array<Tip, NUM_DELEGATES> bsb_vec_new_epoch;
        uint64_t eb_tip_total_RBs;
        //TODO Recall: in case of recall before epoch block, we could have more than two sets of tips

        mutable Log log;
        static constexpr uint32_t WireSize = Tip::WireSize * (1 + 1 + NUM_DELEGATES * 2) + sizeof(eb_tip_total_RBs);
    };
}
