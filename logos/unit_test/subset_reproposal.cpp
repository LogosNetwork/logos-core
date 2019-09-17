#include <gtest/gtest.h>

#include <logos/consensus/request/request_consensus_manager.hpp>

#define Unit_Test_Subset_Reproposal

#ifdef Unit_Test_Subset_Reproposal

auto generate_subsets = [](auto&&... args)
{
    return RequestConsensusManager::GenerateSubsets(args...);
};

TEST (Subset_Reproposal, Test_1)
{
    using WeightList = RequestConsensusManager::WeightList;

    uint128_t prepare_vote  = 0;
    uint128_t prepare_stake = 0;
    uint64_t  request_count = 100;
    uint128_t vote_quorum  = 0;
    uint128_t stake_quorum = 0;
    WeightList response_weights;

    auto reached_quorum = [=](auto vote, auto stake)
    {
        return (vote >= vote_quorum) && (stake >= stake_quorum);
    };

    auto subsets = generate_subsets(prepare_vote,
                                    prepare_stake,
                                    request_count,
                                    response_weights,
                                    reached_quorum);
}

#endif  // #ifdef Unit_Test_Subset_Reproposal