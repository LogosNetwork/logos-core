#pragma once

#include <deque>
#include <unordered_map>
#include <memory>

#include <logos/consensus/messages/messages.hpp>

#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/bootstrap.hpp>
#include <logos/bootstrap/microblock.hpp>
#include <logos/bootstrap/block_cache.hpp>
#include <logos/bootstrap/pull_connection.hpp>

namespace Bootstrap
{
    using PullPtr = std::shared_ptr<PullRequest>;
    using BSBPtr = std::shared_ptr<ApprovedBSB>;
    using MBPtr = std::shared_ptr<ApprovedMB>;
    using EBPtr = std::shared_ptr<ApprovedEB>;

    class Puller
    {
    public:
        Puller(BlockCache & block_cache, Store & store);
        void Init(TipSet &my_tips, TipSet &others_tips);

        PullPtr GetPull();
        bool AllDone();
        size_t GetNumWaitingPulls();

        enum class PullStatus
        {
            Continue,
			Done,
            DisconnectSender,
            BlackListSender,

			Unknown				= 0xff
        };

        /**
         * @param pull the pull that the block belongs to
         * @param block if nullptr, then no more blocks
         * @return if sender of the block should be blacklisted
         */
		//        template<ConsensusType CT>
		//        PullStatus BlockReceived(PullPtr pull,
		//        		uint32_t block_number,
		//        		std::shared_ptr<PostCommittedBlock<CT>> block,
		//				bool last_block);

        PullStatus EBReceived(PullPtr pull, EBPtr block);
        PullStatus MBReceived(PullPtr pull, MBPtr block);
		PullStatus BSBReceived(PullPtr pull, BSBPtr block, bool last_block);
        void PullFailed(PullPtr pull);

    private:
        /*
         * should be called only when both waiting_pulls and ongoing_pulls are empty
         */
        void CreateMorePulls();
        void ExtraMicroBlock();

//        /*
//         * BSB tips only and the two tips should be in the same epoch
//         */
//        uint32_t ComputeNumBSBToPull(Tip &a, Tip &b);

        BlockCache & block_cache;
        Store & store;

        TipSet my_tips;
        TipSet others_tips;

        std::mutex mtx;
        std::deque<PullPtr> waiting_pulls;
        std::unordered_map<PullPtr, uint32_t> ongoing_pulls;//TODO change to hash_set?

        enum class PullState
		{
        	Epoch,
			Micro,
			Batch,
			Done
		};
        PullState state;

        struct MicroPeriod
        {
        	MBPtr mb;
        	std::unordered_set<BlockHash> bsb_tips;
        };

        struct EpochPeriod
        {
        	uint32_t epoch_num;
            EBPtr eb;
            MicroPeriod cur_mb;
            bool need_next_mb;
            MicroPeriod next_mb;//for corner case: bsb of cur_mb depends on bsb in next_mb
        };

        EpochPeriod working_epoch;
    };

    class PullRequestHandler
    {
    public:
        PullRequestHandler(PullPtr request, Store & store);
        bool GetNextSerializedResponse(std::vector<uint8_t> & buf);
    private:

        PullPtr request;
        Store & store;
    };

} //namespace

