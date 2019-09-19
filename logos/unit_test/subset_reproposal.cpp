#include <gtest/gtest.h>

#include <logos/consensus/request/request_consensus_manager.hpp>

#define Unit_Test_Subset_Reproposal

#ifdef Unit_Test_Subset_Reproposal

auto generate_subsets = [](auto&&... args)
{
    return RequestConsensusManager::GenerateSubsets(args...);
};

auto print_subsets = [](const auto & subsets)
{
    for(auto & set : subsets)
    {
        std::vector<uint8_t> delegates(std::get<0>(set).begin(), std::get<0>(set).end());
        std::vector<uint64_t> requests(std::get<1>(set).begin(), std::get<1>(set).end());

        std::sort(delegates.begin(), delegates.end());
        std::sort(requests.begin(), requests.end());

        std::cout << "Delegates: ";

        for(auto delegate : delegates)
        {
            std::cout << int(delegate) << " ";
        }

        std::cout << std::endl;

        std::cout << "Supported requests: ";

        for(auto request : requests)
        {
            std::cout << request << " ";
        }

        std::cout << std::endl;
    }
};

TEST (Subset_Reproposal, Test_1)
{
    using SupportMap = RequestConsensusManager::SupportMap;
    using WeightList = RequestConsensusManager::WeightList;
    using Weights    = RequestConsensusManager::Weights;
    using Delegates  = Weights::Delegates;

    uint128_t prepare_vote  = 10000 * 16;
    uint128_t prepare_stake = 10000 * 16;
    uint64_t  request_count = 6;
    uint128_t vote_total  = 10000 * 32;
    uint128_t stake_total = 10000 * 32;
    uint128_t vote_quorum  = 0;
    uint128_t stake_quorum = 0;
    uint128_t max_vote_fault  = 0;
    uint128_t max_stake_fault = 0;

    PrimaryDelegate::SetQuorum(max_vote_fault, vote_quorum, vote_total);
    PrimaryDelegate::SetQuorum(max_stake_fault,stake_quorum, stake_total);

    Delegates group3 = {16, 17, 18, 19, 20, 21, 22, 23};
    Delegates group4 = {24, 25, 26, 27, 28, 29, 30, 31};

    uint128_t group_weight = 10000 * 8;

    WeightList response_weights = {
        Weights{0, 0, group_weight, group_weight, group3},
        Weights{0, 0, group_weight, group_weight, group3},
        Weights{0, 0, group_weight, group_weight, group3},
        Weights{0, 0, group_weight, group_weight, group4},
        Weights{0, 0, group_weight, group_weight, group4},
        Weights{0, 0, group_weight, group_weight, group4}
    };

    auto reached_quorum = [=](auto vote, auto stake)
    {
        return (vote >= vote_quorum) && (stake >= stake_quorum);
    };

    auto subsets = generate_subsets(prepare_vote,
                                    prepare_stake,
                                    request_count,
                                    response_weights,
                                    reached_quorum);

    std::list<SupportMap> expected_subsets = {
        {
            group3, {0, 1, 2}
        },
        {
            group4, {3, 4, 5}
        }
    };

    ASSERT_EQ(expected_subsets, subsets);
    print_subsets(subsets);
}

TEST (Subset_Reproposal, Test_2)
{
    using SupportMap = RequestConsensusManager::SupportMap;
    using WeightList = RequestConsensusManager::WeightList;
    using Weights    = RequestConsensusManager::Weights;
    using Delegates  = Weights::Delegates;

    uint128_t prepare_vote  = 0;
    uint128_t prepare_stake = 0;
    uint64_t  request_count = 1000;
    uint128_t vote_total  = 10000 * 32;
    uint128_t stake_total = 10000 * 32;
    uint128_t vote_quorum  = 0;
    uint128_t stake_quorum = 0;
    uint128_t max_vote_fault  = 0;
    uint128_t max_stake_fault = 0;

    PrimaryDelegate::SetQuorum(max_vote_fault, vote_quorum, vote_total);
    PrimaryDelegate::SetQuorum(max_stake_fault,stake_quorum, stake_total);

    Delegates group_a = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
    Delegates group_b = {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

    uint128_t group_weight = 10000 * 24;

    WeightList response_weights;

    for(uint64_t i = 0; i < 1000; ++i)
    {
        auto & group = i < 500 ? group_a : group_b;

        response_weights[i] = Weights{0, 0, group_weight, group_weight, group};
    }

    auto reached_quorum = [=](auto vote, auto stake)
    {
        return (vote >= vote_quorum) && (stake >= stake_quorum);
    };

    auto subsets = generate_subsets(prepare_vote,
                                    prepare_stake,
                                    request_count,
                                    response_weights,
                                    reached_quorum);

    std::list<SupportMap> expected_subsets = {
        {
            group_a, {}
        },
        {
            group_b, {}
        }
    };

    auto itr = expected_subsets.begin();

    for(uint64_t i = 0; i < 1000; ++i)
    {
        if(i == 500)
        {
            ++itr;
        }

        std::get<1>(*itr).insert(i);
    }

    ASSERT_EQ(expected_subsets, subsets);
    print_subsets(subsets);
}

TEST (Subset_Reproposal, Test_3)
{
    using SupportMap = RequestConsensusManager::SupportMap;
    using WeightList = RequestConsensusManager::WeightList;
    using Weights    = RequestConsensusManager::Weights;
    using Delegates  = Weights::Delegates;

    uint128_t prepare_vote  = 0;
    uint128_t prepare_stake = 0;
    uint64_t  request_count = 21;
    uint128_t vote_total  = 10000 * 32;
    uint128_t stake_total = 10000 * 32;
    uint128_t vote_quorum  = 0;
    uint128_t stake_quorum = 0;
    uint128_t max_vote_fault  = 0;
    uint128_t max_stake_fault = 0;

    PrimaryDelegate::SetQuorum(max_vote_fault, vote_quorum, vote_total);
    PrimaryDelegate::SetQuorum(max_stake_fault,stake_quorum, stake_total);

    Delegates group = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

    uint128_t group_weight = 10000 * 24;

    WeightList response_weights;

    for(uint64_t i = 0; i < 21; ++i, group_weight += 10000)
    {
        response_weights[i] = Weights{0, 0, group_weight, group_weight, group};
        group.insert(21 + i);
    }

    auto reached_quorum = [=](auto vote, auto stake)
    {
        return (vote >= vote_quorum) && (stake >= stake_quorum);
    };

    auto subsets = generate_subsets(prepare_vote,
                                    prepare_stake,
                                    request_count,
                                    response_weights,
                                    reached_quorum);

    std::list<SupportMap> expected_subsets = {
        {
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}
        }
    };

    ASSERT_EQ(expected_subsets, subsets);
    print_subsets(subsets);
}

TEST (Subset_Reproposal, Test_4)
{
    using SupportMap = RequestConsensusManager::SupportMap;
    using WeightList = RequestConsensusManager::WeightList;
    using Weights    = RequestConsensusManager::Weights;
    using Delegates  = Weights::Delegates;

    uint128_t prepare_vote  = 0;
    uint128_t prepare_stake = 0;
    uint64_t  request_count = 21;
    uint128_t vote_total  = 10000 * 32;
    uint128_t stake_total = 10000 * 32;
    uint128_t vote_quorum  = 0;
    uint128_t stake_quorum = 0;
    uint128_t max_vote_fault  = 0;
    uint128_t max_stake_fault = 0;

    PrimaryDelegate::SetQuorum(max_vote_fault, vote_quorum, vote_total);
    PrimaryDelegate::SetQuorum(max_stake_fault,stake_quorum, stake_total);

    uint128_t group_weight = 10000 * 24;

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

    std::list<SupportMap> expected_subsets;

    ASSERT_EQ(expected_subsets, subsets);
    print_subsets(subsets);
}

#endif  // #ifdef Unit_Test_Subset_Reproposal