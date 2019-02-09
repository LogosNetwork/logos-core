#include <gtest/gtest.h>

#include <logos/consensus/primary_delegate.hpp>


TEST (TallyingTest, VerifyThreshold)
{
    using uint128_t = logos::uint128_t;
    uint128_t max_fault, quorum, total;
#ifdef STRICT_CONSENSUS_THRESHOLD
    total = 1;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (0, max_fault);
    EXPECT_EQ (1, quorum);
    total = 10;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (0, max_fault);
    EXPECT_EQ (total, quorum);
    total = 100000000000;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (0, max_fault);
    EXPECT_EQ (total, quorum);
#else
    total = 1;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (0, max_fault);
    EXPECT_EQ (1, quorum);
    total = 10;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (3, max_fault);
    EXPECT_EQ (7, quorum);
    total = 11;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (3, max_fault);
    EXPECT_EQ (7, quorum);
    total = 12;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (3, max_fault);
    EXPECT_EQ (7, quorum);
    total = 100000000000;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (33333333333, max_fault);
    EXPECT_EQ (66666666667, quorum);
    total = 100000000001;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (33333333333, max_fault);
    EXPECT_EQ (66666666667, quorum);
    total = 100000000002;
    PrimaryDelegate::SetQuorum(max_fault, quorum, total);
    EXPECT_EQ (33333333333, max_fault);
    EXPECT_EQ (66666666667, quorum);
#endif
}
