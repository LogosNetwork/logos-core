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
         * @return status of the pull
         */
        PullStatus EBReceived(PullPtr pull, EBPtr block);
        PullStatus MBReceived(PullPtr pull, MBPtr block);
		PullStatus BSBReceived(PullPtr pull, BSBPtr block, bool last_block);
        void PullFailed(PullPtr pull);

    private:
        void CreateMorePulls();

        void UpdateMyBSBTip(BSBPtr block);
        void UpdateMyMBTip(MBPtr block);
        void UpdateMyEBTip(EBPtr block);

        void CheckProgress()
        {
			assert(working_epoch.cur_mbp.bsb_targets.empty());
			if(working_epoch.two_mbps)
			{
				assert(working_epoch.cur_mbp.mb != nullptr);
				assert(working_epoch.next_mbp.bsb_targets.empty());

				auto digest(working_epoch.cur_mbp.mb->Hash());
				bool mb_processed = !block_cache.IsMBCached(working_epoch.epoch_num, digest);
				if(mb_processed)
				{
					working_epoch.cur_mbp = working_epoch.next_mbp;
					working_epoch.two_mbps = false;
					working_epoch.next_mbp.Clean();
				}
				else
				{
					LOG_FATAL(log) << "Puller::CreateMorePulls: pulled two MB periods,"
									<< " but first MB has not been processed."
									<< " epoch_num=" << working_epoch.epoch_num
									<< " MB_1 hash=" << digest;
					trace_and_halt();
				}
			}

			if(working_epoch.cur_mbp.mb != nullptr)
			{
				auto digest(working_epoch.cur_mbp.mb->Hash());
				bool mb_processed = !block_cache.IsMBCached(working_epoch.epoch_num, digest);
				if(mb_processed)
				{
					if(working_epoch.cur_mbp.mb->last_micro_block)
					{
						if(working_epoch.eb != nullptr)
						{
							bool eb_processed = !block_cache.IsEBCached(working_epoch.epoch_num);
							if(eb_processed)
							{
								LOG_INFO(log) << "Puller::BSBReceived: processed an epoch "<< working_epoch.epoch_num;
							}
							else
							{
								LOG_FATAL(log) << "Puller::BSBReceived: cannot process epoch block after last micro block "
											   << working_epoch.epoch_num;
								trace_and_halt();
							}
						}
						else
						{
							//TODO add verification that we are in the next to last epoch after we can verify other's tips
							LOG_WARN(log) << "Puller::BSBReceived: have last MB but not EB "<< working_epoch.epoch_num;
						}
						state = PullState::Epoch;
					}
					else
					{
						state = PullState::Micro;
					}
					working_epoch.cur_mbp.Clean();
				}
				else
				{
					working_epoch.two_mbps = true;
					state = PullState::Micro;
				}
			}
        }

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
        bool GetNextSerializedResponse(std::vector<uint8_t> & buf);
    private:

        PullPtr request;
        Store & store;
    };

} //namespace

//        /*
//         * BSB tips only and the two tips should be in the same epoch
//         */
//        uint32_t ComputeNumBSBToPull(Tip &a, Tip &b);
//        template<ConsensusType CT>
//        PullStatus BlockReceived(PullPtr pull,
//        		uint32_t block_number,
//        		std::shared_ptr<PostCommittedBlock<CT>> block,
//				bool last_block);

