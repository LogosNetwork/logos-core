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

		Unknown				= 0xff
    };

    class Puller
    {
    public:
        Puller(IBlockCache & block_cache);
        void Init(TipSet &my_tips, TipSet &others_tips);

        PullPtr GetPull();
        bool AllDone();
        size_t GetNumWaitingPulls();

        /**
         * @param pull the pull that the block belongs to
         * @param block the block
         * @return status of the pull
         */
        PullStatus EBReceived(PullPtr pull, EBPtr block);
        PullStatus MBReceived(PullPtr pull, MBPtr block);
		PullStatus BSBReceived(PullPtr pull, BSBPtr block, bool last_block);
        void PullFailed(PullPtr pull);

    private:
        void CreateMorePulls();
        void CheckMicroProgress();

        void UpdateMyBSBTip(BSBPtr block);
        void UpdateMyMBTip(MBPtr block);
        void UpdateMyEBTip(EBPtr block);

        IBlockCache & block_cache;
        TipSet my_tips;
        TipSet others_tips;

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
            //for corner case: bsb of cur_mbp depends on bsb in next_mbp
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
        PullRequestHandler(PullRequest request, Store & store);

        //return: if true call again for more blocks
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
