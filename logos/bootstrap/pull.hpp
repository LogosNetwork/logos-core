#pragma once

#include <deque>
#include <unordered_map>
#include <memory>

#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/persistence/block_cache.hpp>

#include <logos/bootstrap/bootstrap_messages.hpp>
//#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/microblock.hpp>
//#include <logos/bootstrap/pull_connection.hpp>

namespace Bootstrap
{
    using PullPtr = std::shared_ptr<PullRequest>;
    using BSBPtr = std::shared_ptr<ApprovedBSB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    enum class PullStatus
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
        Puller(BlockCache & block_cache, Store & store);
        void Init(TipSet &my_tips, TipSet &others_tips);

        PullPtr GetPull();
        bool AllDone();
        size_t GetNumWaitingPulls();

        /**
         * @param pull the pull that the block belongs to
         * @param block if nullptr, then no more blocks
         * @return status of the pull
         */
        PullStatus EBReceived(PullPtr pull, EBPtr block);
        PullStatus MBReceived(PullPtr pull, MBPtr block);
		PullStatus BSBReceived(PullPtr pull, BSBPtr block, bool last_block);
        void PullFailed(PullPtr pull);

    private:
        void CreateMorePulls();
        void CheckProgress();

        void UpdateMyBSBTip(BSBPtr block);
        void UpdateMyMBTip(MBPtr block);
        void UpdateMyEBTip(EBPtr block);

        BlockCache & block_cache;
        Store & store;

        TipSet my_tips;
        TipSet others_tips;

        std::mutex mtx;
        std::deque<PullPtr> waiting_pulls;
        std::unordered_set<PullPtr> ongoing_pulls;

        enum class PullState
		{
        	Epoch,
			Micro,
			Batch,
			Batch_No_MB,
			Done
		};
        PullState state;

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
            bool two_mbps;
            MicroPeriod next_mbp;//for corner case: bsb of cur_mb depends on bsb in next_mb
        };
        EpochPeriod working_epoch;

        Log log;
    };

    class PullRequestHandler
    {
    public:
        PullRequestHandler(PullPtr request, Store & store);

        //TODO rate limit the requester?
        //return: if true call again for more blocks
        bool GetNextSerializedResponse(std::vector<uint8_t> & buf);

    private:
        uint32_t GetBlock(BlockHash & hash, std::vector<uint8_t> & buf);

        PullPtr request;
        Store & store;
        BlockHash next;
    };

} //namespace
