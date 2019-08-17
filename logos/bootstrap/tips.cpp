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
            new (&bsb_vec[i]) Tip(error, stream);
            if (error) {
                return;
            }
        }
        for (uint8_t i = 0; i < NUM_DELEGATES; ++i)
        {
            new (&bsb_vec_new_epoch[i]) Tip(error, stream);
            if (error) {
                return;
            }
        }

        error = logos::read(stream, eb_tip_total_RBs);
    }

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

        s += logos::write(stream, eb_tip_total_RBs);
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

                if(eb_tip_total_RBs != other.eb_tip_total_RBs)
                {
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

    uint32_t compute_num_RBs_in_epoch(const std::array<Tip, NUM_DELEGATES> & rb_vec, uint32_t expected_epoch)
    {
        uint32_t rbs = 0;
        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            rbs += rb_vec[i].n_th_block_in_epoch(expected_epoch);
        }
        return rbs;

    }

    uint64_t TipSet::ComputeNumberAllRBs() const
    {
        return eb_tip_total_RBs +
               compute_num_RBs_in_epoch(bsb_vec, eb.epoch+1) +
               compute_num_RBs_in_epoch(bsb_vec_new_epoch, eb.epoch+2);
    }


    bool TipSet::ValidTips() const
    {
        //the epoch # of MB can be greater than the epoch # of EB by at most 1.
        if(mb.epoch > eb.epoch + 1)
        {
            LOG_DEBUG(log) << "TipSet::"<<__func__ << " bad tips, MB and EB epoch number mismatch";
            return false;
        }

        //valid EB's digest cannot be 0
        if (eb.epoch != 0 && eb.digest == 0)
        {
            LOG_DEBUG(log) << "TipSet::" << __func__ << " bad tips, eb sqn != 0, but digest == 0";
            return false;
        }

        //valid MB's digest cannot be 0
        if (mb.sqn != 0 && mb.digest == 0)
        {
            LOG_DEBUG(log) << "TipSet::" << __func__ << " bad tips, mb sqn != 0, but digest == 0";
            return false;
        }

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            //the epoch # of RBs in bsb_vec or bsb_vec_new_epoch cannot be too far from the epoch # of the EB
            if(bsb_vec[i].epoch > eb.epoch + 1 ||
               bsb_vec_new_epoch[i].epoch > eb.epoch + 2 )
            {
                LOG_DEBUG(log) << "TipSet::"<<__func__ << " bad tips, RB EB epoch number mismatch";
                return false;
            }

            //the epoch # of RBs in bsb_vec or bsb_vec_new_epoch cannot be too far from the epoch # of the MB
            if(bsb_vec[i].epoch > mb.epoch  + 1 ||
               bsb_vec_new_epoch[i].epoch > mb.epoch + 2 )
            {
                LOG_DEBUG(log) << "TipSet::"<<__func__ << " bad tips, RB MB epoch number mismatch";
                return false;
            }

            //the tips in bsb_vec cannot be behind comparing to tips in bsb_vec_new_epoch
            if ((! bsb_vec_new_epoch[i].digest.is_zero()) && bsb_vec_new_epoch[i] < bsb_vec[i])
            {
                LOG_DEBUG(log) << "TipSet::" << __func__ << " bad tips, tip in new epoch is behind";
                return false;
            }

            //valid RB's digest cannot be 0
            if (bsb_vec[i].sqn != 0 && bsb_vec[i].digest == 0)
            {
                LOG_DEBUG(log) << "TipSet::" << __func__ << " bad tips, rb sqn != 0, but digest == 0";
                return false;
            }
            //valid RB's digest cannot be 0
            if (bsb_vec_new_epoch[i].sqn != 0 && bsb_vec_new_epoch[i].digest == 0)
            {
                LOG_DEBUG(log) << "TipSet::" << __func__ << " bad tips, rb sqn != 0, but digest == 0";
                return false;
            }
        }
        return true;
    }

    bool TipSet::ValidPeerTips(const TipSet & others) const
    {
        //LOG_TRACE(log) << "TipSet::"<<__func__;
        if( ! others.ValidTips())
        {
            LOG_DEBUG(log) << "TipSet::" << __func__ << " bad others tips by itself";
            return false;
        }

        if(eb.epoch <= others.eb.epoch - 2)
        {
            if(others.eb_tip_total_RBs < ComputeNumberAllRBs())
            {
                LOG_DEBUG(log) << "TipSet::"<<__func__ << " bad others tips (case -2), wrong number of RBs";
                return false;
            }
        }
        else if(eb.epoch == others.eb.epoch - 1)
        {
            if(others.eb_tip_total_RBs < (eb_tip_total_RBs + compute_num_RBs_in_epoch(bsb_vec, eb.epoch+1)))
            {
                LOG_DEBUG(log) << "TipSet::"<<__func__ << " bad others tips (case -1), wrong number of RBs";
                return false;
            }
        }
        else if(eb.epoch == others.eb.epoch)
        {
            if(others.eb_tip_total_RBs != eb_tip_total_RBs)
            {
                LOG_DEBUG(log) << "TipSet::"<<__func__ << " bad others tips (case 0), wrong number of RBs";
                return false;
            }
        }
        else if(eb.epoch == others.eb.epoch + 1)
        {
            if((others.eb_tip_total_RBs + compute_num_RBs_in_epoch(others.bsb_vec, others.eb.epoch+1)) >
                eb_tip_total_RBs)
            {
                LOG_DEBUG(log) << "TipSet::"<<__func__ << " bad others tips (case +1), wrong number of RBs";
                return false;
            }
        }
        else
        {
            if(others.ComputeNumberAllRBs() > eb_tip_total_RBs)
            {
                LOG_DEBUG(log) << "TipSet::"<<__func__ << " bad others tips (case +2), wrong number of RBs";
                return false;
            }
        }
        return true;
    }

    void TipSet::ComputeNumberBlocksBehind(const TipSet & other, uint32_t & num_eb, uint32_t & num_mb, uint64_t & num_rb) const
    {
        num_eb = eb.epoch < other.eb.epoch ? other.eb.epoch - eb.epoch : 0;
        num_mb = mb.sqn < other.mb.sqn ? other.mb.sqn - mb.sqn : 0;

        /*
         * 5 cases, because of the complications:
         * -- usually 32 chains, but 64 chains during epoch transition
         * -- sqn reset at beginning of epoch
         * -- A could have more on chain_1, and B could have more on chain_2
         *
         * case 1) other has two or more EBs
         * case 2) other has one more EB
         * case 3) we have the same EB
         * case 4) we have one more EB
         * case 5) we have two or more EBs
         */
        if(eb.epoch <= other.eb.epoch - 2) //case 1
        {
            /*
             * impossible for any chain being longer than other's
             * num_rb = other.all - my.all
             */
            // don't assert basing on peer message, move assert to ValidPeerTips
            // assert(other.ComputeNumberAllRBs() >= ComputeNumberAllRBs());
            num_rb = other.ComputeNumberAllRBs() - ComputeNumberAllRBs();
        }
        else if (eb.epoch == other.eb.epoch - 1) //case 2
        {
            /*
             * let i = eb.epoch
             * need to consider 3 cases:
             * 1) epochs <= i+1,
             * 2) epoch == i+2,
             * 3) epoch == i+3
             */
            // case 1
            auto my_1 = eb_tip_total_RBs;
            for (uint i = 0; i < NUM_DELEGATES; ++i) {
                my_1 += bsb_vec[i].n_th_block_in_epoch(eb.epoch + 1);
            }
            auto other_1 = other.eb_tip_total_RBs;
            // don't assert basing on peer message,
            // assert(other_1 >= my_1);
            auto diff_1 = other_1 - my_1;

            // case 2
            uint64_t diff_2 = 0;
            for(uint i = 0; i < NUM_DELEGATES; ++i)
            {
                auto my_n = bsb_vec_new_epoch[i].n_th_block_in_epoch(eb.epoch+2);
                auto other_n = other.bsb_vec[i].n_th_block_in_epoch(eb.epoch+2);
                if(other_n > my_n)
                {
                    diff_2 += other_n - my_n;
                }
            }

            // case 3
            uint64_t diff_3 = 0;
            for (uint i = 0; i < NUM_DELEGATES; ++i) {
                diff_3 += other.bsb_vec_new_epoch[i].n_th_block_in_epoch(eb.epoch + 3);
            }

            num_rb = diff_1 + diff_2 + diff_3;
        }
        else if (eb.epoch == other.eb.epoch) //case 3
        {
            /*
             * only need to compare the two sets of tips
             */
            num_rb = 0;
            for(uint i = 0; i < NUM_DELEGATES; ++i)
            {
                auto my_n = bsb_vec[i].n_th_block_in_epoch(eb.epoch+1);
                auto other_n = other.bsb_vec[i].n_th_block_in_epoch(eb.epoch+1);
                if(other_n > my_n)
                {
                    num_rb += other_n - my_n;
                }
            }
            for(uint i = 0; i < NUM_DELEGATES; ++i)
            {
                auto my_n = bsb_vec_new_epoch[i].n_th_block_in_epoch(eb.epoch+2);
                auto other_n = other.bsb_vec_new_epoch[i].n_th_block_in_epoch(eb.epoch+2);
                if(other_n > my_n)
                {
                    num_rb += other_n - my_n;
                }
            }
        }
        else if (eb.epoch == other.eb.epoch + 1) //case 4
        {
            /*
             * only need to consider eb.epoch+1, because other does not have eb.epoch+2
             */
            num_rb = 0;
            for(uint i = 0; i < NUM_DELEGATES; ++i)
            {
                auto my_n = bsb_vec[i].n_th_block_in_epoch(eb.epoch+1);
                auto other_n = other.bsb_vec_new_epoch[i].n_th_block_in_epoch(eb.epoch+1);
                if(other_n > my_n)
                {
                    num_rb += other_n - my_n;
                }
            }
        }
        else //case 5
        {
            num_rb = 0;
        }
    }


    /*
     * We assume both a and b are valid tips, in this iteration of the bootstrapping.
     * One of the TODOs in the next release of bootstrapping is to validate the peer's tips.
     * We could ask the peer for all the approved blocks included in the tips.
     * If all the blocks have valid agg-signature, then we consider the peer's tips are valid.
     */
    bool TipSet::IsBehind(const TipSet & other) const
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

    uint32_t TipSet::GetLatestEpochNumber() const
    {
        uint32_t e = std::max(eb.epoch, mb.epoch);
        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            e = std::max(e, bsb_vec[i].epoch);
            e = std::max(e, bsb_vec_new_epoch[i].epoch);
        }

        return e;
    }

    TipSet TipSet::CreateTipSet(Store & store, bool write_transaction)
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
         *        from Mar 19, 2019 discussion:
         *        (i) on network launch, 32 empty bsbs by 32 delegates are created
         *        TODO see if can get rid of (i)
         *        (ii) old tips are carried over if nothing created in new epoch
         * 5) assert e#_e+1==e#_m
         *       if micro is the last of an epoch, goto 6), else 7)
         * 6) (unlikely, but could happen. last micro stored, but no epoch)
         *       use e#_m and [0,31] to get 32 bsb tips and put in bsb_vec
         *       use e#_m+1 and [0,31] to get 32 bsb tips and put in
         *           bsb_vec_new_epoch, if not available, zero out that slot
         * 7) same as 6)
         *
         * so we only have two cases:
         * if e#_e==e#_m, goto 4), else 6)
         */

        TipSet tips;
        Log log;
        memset(&tips, 0, sizeof(tips));
        //using a write transaction
        logos::transaction transaction (store.environment, nullptr, write_transaction);

        if(store.epoch_tip_get(tips.eb, transaction))
        {
            LOG_FATAL(log) << "TipSet::CreateTipSet cannot get epoch tip";
            trace_and_halt();
        }

        ApprovedEB epoch_block;
        if(store.epoch_get(tips.eb.digest, epoch_block, transaction))
        {
            LOG_FATAL(log) << "TipSet::CreateTipSet cannot get last epoch";
            trace_and_halt();
        }
        tips.eb_tip_total_RBs = epoch_block.total_RBs;

        if(store.micro_block_tip_get(tips.mb, transaction))
        {
            LOG_FATAL(log) << "TipSet::CreateTipSet cannot get micro tip";
            trace_and_halt();
        }

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
