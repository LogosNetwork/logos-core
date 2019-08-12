#include <gtest/gtest.h>
#include <logos/lib/epoch_time_util.hpp>

#include <logos/consensus/primary_delegate.hpp>

#define Unit_Test_Timers

#ifdef Unit_Test_Timers


TEST(Timers, timers)
{
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(1,0),60);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(2,0), 740);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(2,5),940);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(3,0), 2460);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(3,10), 7860);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(4,0), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(4,12), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(4,28), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(5,0), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(5,12), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::MicroBlock>(6,11), 19200);

    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(1,0),60);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(2,0), 740);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(2,5),940);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(3,0), 2460);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(3,10), 7860);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(4,0), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(4,12), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(4,28), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(5,0), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(5,12), 19200);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Epoch>(6,11), 19200);


    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Request>(1), 60);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Request>(2), 120);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Request>(3), 240);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Request>(4), 480);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Request>(5), 600);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Request>(6), 600);
    ASSERT_EQ(EpochTimeUtil::GetTimeout<ConsensusType::Request>(7), 600);

}


#endif
