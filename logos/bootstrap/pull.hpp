#pragma once

#include <deque>
#include <unordered_map>
#include <memory>

#include <logos/consensus/persistence/block_cache.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>


namespace Bootstrap
{
    using PullPtr = std::shared_ptr<PullRequest>;
    using BSBPtr = std::shared_ptr<ApprovedRB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    enum class PullStatus : uint8_t
    {
        Continue,
        Done,
        DisconnectSender,
        BlackListSender,

        Unknown                = 0xff
    };

    class Puller
    {
    public:
        /**
         * constructor
         * @param block_cache the block cache
         */
        Puller(logos::IBlockCache & block_cache);

        /**
         * initialize the puller
         * @param my_tips my tips
         * @param others_tips peer's tips
         */
        bool Init(const TipSet &my_tipset, const TipSet &others_tipset);

        /**
         * get a pull request
         * @return shared_ptr of a pull request
         */
        PullPtr GetPull();

        /**
         * if the bootstrap is completed
         * @return true if the bootstrap is completed
         */
        bool AllDone();

        /**
         * get the number of waiting pull requests
         * @return the number of waiting pull requests
         */
        size_t GetNumWaitingPulls();

        /**
         * an epoch block is received
         * @param pull the pull request that the block belongs to
         * @param block the block
         * @return status of the pull
         */
        PullStatus EBReceived(PullPtr pull, EBPtr block);

        /**
         * a micro block is received
         * @param pull the pull request that the block belongs to
         * @param block the block
         * @return status of the pull
         */
        PullStatus MBReceived(PullPtr pull, MBPtr block);

        /**
         * a request block is received
         * @param pull the pull request that the block belongs to
         * @param block the block
         * @param last_block if the block is the last block that we can get
         * @return status of the pull
         */
        PullStatus BSBReceived(PullPtr pull, BSBPtr block, bool last_block);

        /**
         * the peer failed to provide more blocks
         * @param pull the pull request
         */
        void PullFailed(PullPtr pull);

        bool GetTipsets(TipSet &my, TipSet &others);

    private:
        void CreateMorePulls();
        void CheckMicroProgress();
        bool ReduceNumBlockToDownload();

        void UpdateMyBSBTip(BSBPtr block);
        void UpdateMyMBTip(MBPtr block);
        void UpdateMyEBTip(EBPtr block);

        logos::IBlockCache & block_cache;
        TipSet my_tips;
        TipSet others_tips;
        uint64_t num_blocks_to_download;
        bool inited;
        std::mutex mtx;//for waiting_pulls and ongoing_pulls
        std::deque<PullPtr> waiting_pulls;
        std::unordered_set<PullPtr> ongoing_pulls;

        enum class PullerState : uint8_t
        {
            Epoch,
            Micro,
            Batch,
            Batch_No_MB,
            Done
        };
        PullerState state = PullerState::Done;

        struct MicroPeriod
        {
            MBPtr mb;
            std::unordered_set<BlockHash> bsb_targets;

            void Clean()
            {
                mb = nullptr;
                assert(bsb_targets.empty());
            }
        };
        struct EpochPeriod
        {
            EpochPeriod(uint32_t epoch_num = 0)
            : epoch_num(epoch_num)
            , two_mbps(false)
            {}

            uint32_t epoch_num;
            EBPtr eb;
            MicroPeriod cur_mbp;
            /*
             * for corner case: bsb of cur_mbp depends on bsb in next_mbp.
             * in more detail, because of time drift allowed in the system,
             * there is a chance that an earlier request block A proposed by
             * delegate X has a later timestamp comparing to another block B
             * proposed by Y. If that happens, then there is a small chance that
             * B is included in a micro block and A is not (due to the later
             * timestamp). There is also a small chance that two requests of the
             * same account r1 and r2 end up in block A and block B respectively.
             * If all above happens, the earlier micro block will have a
             * dependency on a request block that is not included in this micro
             * block.
             * Note that the last micro block of an epoch uses epoch_number field
             * in the block to cut off. So this cornor case will not happen
             * cross the epoch boundary.
             */
            bool two_mbps;
            MicroPeriod next_mbp;
        };
        EpochPeriod working_epoch;
        uint32_t final_ep_number = 0;

        Log log;
    };

    class PullRequestHandler
    {
    public:

        /**
         * constructor
         * @param request the pull request
         * @param store the database
         */
        PullRequestHandler(PullRequest request, Store & store);

        //return: if true call again for more blocks

        /**
         * Get the next serialized pull response
         * @param buf the data buffer that will be filled with a pull response
         * @return true if the caller should call again for more blocks
         */
        bool GetNextSerializedResponse(std::vector<uint8_t> & buf);

    private:
        uint32_t GetBlock(BlockHash & hash, std::vector<uint8_t> & buf);
        void TraceToEpochBegin();

        PullRequest request;
        Store & store;
        BlockHash next;
        Log log;
    };

} //namespace
