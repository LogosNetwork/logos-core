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
    	/**
    	 * constructor
    	 * @param block_cache the block cache
    	 */
        Puller(IBlockCache & block_cache);

        /**
         * initialize the puller
         * @param my_tips my tips
         * @param others_tips peer's tips
         */
        void Init(TipSet &my_tips, TipSet &others_tips);

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
