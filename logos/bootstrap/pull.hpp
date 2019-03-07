#pragma once

#include <deque>
#include <unordered_set>

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
        void Init(const TipSet &my_tips, const TipSet &others_tips);

        PullPtr GetPull(/*bool & all_done*/);

        bool AllDone();
        size_t GetNumWaitingPulls();

        /**
         * @param pull the pull that the block belongs to
         * @param block if nullptr, then no more blocks
         * @return if sender of the block should be blacklisted
         */
        bool MBReceived(PullPtr pull, MBPtr block);
        bool EBReceived(PullPtr pull, EBPtr block);
        bool BSBReceived(PullPtr pull, BSBPtr block);

    private:
        void CreateMorePulls();

        BlockCache & block_cache;
        Store & store;

        TipSet my_tips;
        TipSet others_tips;

        std::mutex mutex;
        std::deque<PullPtr> waiting_pulls;
        std::unordered_map<PullPtr, uint32_t> ongoing_pulls;
    };

    class PullRequestHandler
    {
    public:
        PullRequestHandler(PullRequest &request, Store & store);
        void GetNextSerializedResponse(std::vector<uint8_t> & buf);
    private:

        PullRequest request;
        Store & store;
    };

} //namespace

