#include <gtest/gtest.h>
#include <vector>

#include <logos/bootstrap/attempt.hpp>
#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/pull.hpp>
#include <logos/bootstrap/tips.hpp>
#include <logos/node/common.hpp>
#include <logos/node/node.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/persistence/block_cache.hpp>


//#include <logos/consensus/persistence/batchblock/nondel_batchblock_persistence.hpp>
//#include <logos/consensus/persistence/epoch/nondel_epoch_persistence.hpp>
//#include <logos/consensus/persistence/microblock/nondel_microblock_persistence.hpp>


TEST (bootstrap, msg_header)
{
    Bootstrap::MessageHeader header(0,
            Bootstrap::MessageType::TipResponse,
            ConsensusType::MicroBlock,
            123);

    std::vector<uint8_t> buf;
    {
        logos::vectorstream write_stream(buf);
        header.Serialize(write_stream);
    }
    std::cout << "buf.size=" << buf.size() << std::endl;
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size());
        Bootstrap::MessageHeader header2(error, read_stream);
        ASSERT_FALSE(error);
        ASSERT_EQ(memcmp(&header, &header2, Bootstrap::MessageHeader::WireSize), 0);
    }
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size()-1);
        Bootstrap::MessageHeader header2(error, read_stream);
        ASSERT_TRUE(error);
    }
}

TEST (bootstrap, msg_tip)
{
    Tip tip(123,234,345);
    std::vector<uint8_t> buf;
    {
        logos::vectorstream write_stream(buf);
        tip.Serialize(write_stream);
    }
    std::cout << "buf.size=" << buf.size() << std::endl;
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size());
        Tip tip2(error, read_stream);
        ASSERT_FALSE(error);
        //        ASSERT_EQ(memcmp(&tip, &tip2, sizeof(tip2)), 0);
        ASSERT_EQ(tip, tip2);
    }
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size()-1);
        Tip tip2(error, read_stream);
        ASSERT_TRUE(error);
    }
}


static Bootstrap::TipSet create_tip_set()
{
    Bootstrap::TipSet tips;
    uint32_t epoch_num = 2;
    uint32_t mb_sqn = 3;

    tips.eb = {epoch_num, epoch_num, 3};
    tips.mb = {epoch_num+1, mb_sqn, 4};

    for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
        tips.bsb_vec[i] = {epoch_num+1, 0, 0};
    }
    for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
        tips.bsb_vec_new_epoch[i] = {epoch_num+2, 0, 0};
    }
    tips.eb_tip_total_RBs = 0;
    return tips;
}

TEST (bootstrap, msg_tip_set)
{
    Bootstrap::TipSet tips = create_tip_set();

    std::vector<uint8_t> buf;
    {
        logos::vectorstream write_stream(buf);
        tips.Serialize(write_stream);
    }
    std::cout << "buf.size=" << buf.size() << std::endl;
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size());
        Bootstrap::TipSet tips2(error, read_stream);
        ASSERT_FALSE(error);
        ASSERT_EQ(tips, tips2);
    }
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size()-1);
        Bootstrap::TipSet tips2(error, read_stream);
        ASSERT_TRUE(error);
    }
}

TEST (bootstrap, msg_pull_request)
{
    Bootstrap::PullRequest request (ConsensusType::MicroBlock, 22, 23);
    std::vector<uint8_t> buf;
    {
        logos::vectorstream write_stream(buf);
        request.Serialize(write_stream);
    }
    std::cout << "buf.size=" << buf.size() << std::endl;
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size());
        Bootstrap::PullRequest request2(error, read_stream);
        ASSERT_FALSE(error);
        ASSERT_EQ(request, request2);
    }
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size()-1);
        Bootstrap::PullRequest request2(error, read_stream);
        ASSERT_TRUE(error);
    }
}

TEST (bootstrap, msg_pull_response)
{
    {
        Bootstrap::PullResponse<ConsensusType::Request> block;
        block.status = Bootstrap::PullResponseStatus::NoBlock;

        std::vector<uint8_t> buf;
        {
            logos::vectorstream write_stream(buf);
            block.Serialize(write_stream);
        }
        std::cout << "buf.size=" << buf.size() << std::endl;
        {
            bool error = false;
            logos::bufferstream read_stream(buf.data(), buf.size());
            Bootstrap::PullResponse<ConsensusType::Request> block2(error, read_stream);
            ASSERT_FALSE(error);
            ASSERT_EQ(block, block2);
        }
        {
            bool error = false;
            logos::bufferstream read_stream(buf.data(), buf.size()-1);
            Bootstrap::PullResponse<ConsensusType::Request> request2(error, read_stream);
            ASSERT_TRUE(error);
        }
    }

    {//pull server copy block without re-serialize
        Bootstrap::PullResponse<ConsensusType::MicroBlock> block;
        block.status = Bootstrap::PullResponseStatus::MoreBlock;
        block.block.reset(new PostCommittedBlock<ConsensusType::MicroBlock>());
        block.block->epoch_number = 123;

        std::vector<uint8_t> block_buf;
        {
            logos::vectorstream write_stream(block_buf);
            block.block->Serialize(write_stream, true, true);//approved block only
        }
        std::cout << "block_buf.size=" << block_buf.size() << std::endl;
        {
            std::vector<uint8_t> buf_sent(Bootstrap::PullResponseReserveSize+block_buf.size());
            memcpy(buf_sent.data() + Bootstrap::PullResponseReserveSize, block_buf.data(), block_buf.size());
            Bootstrap::PullResponseSerializedLeadingFields(ConsensusType::MicroBlock,
                    Bootstrap::PullResponseStatus::MoreBlock, block_buf.size(), buf_sent);

            bool error = false;
            logos::bufferstream read_stream(buf_sent.data(), buf_sent.size());
            Bootstrap::MessageHeader header(error, read_stream);
            ASSERT_EQ(header.pull_response_ct, ConsensusType::MicroBlock);
            ASSERT_EQ(header.type, Bootstrap::MessageType::PullResponse);
            Bootstrap::PullResponse<ConsensusType::MicroBlock> block2(error, read_stream);
            ASSERT_FALSE(error);
            ASSERT_EQ(block, block2);
            ASSERT_EQ(block.block->Hash(), block2.block->Hash());
        }
    }

}

TEST (bootstrap, tip_set_compute_num_rb)
{
    Bootstrap::TipSet tips = create_tip_set();

    ASSERT_EQ(tips.ComputeNumberAllRBs(), 0);

    tips.bsb_vec[0].digest = 1;
    ASSERT_EQ(tips.ComputeNumberAllRBs(), 1);

    tips.bsb_vec_new_epoch[0].digest = 1;
    ASSERT_EQ(tips.ComputeNumberAllRBs(), 2);

    tips.bsb_vec[1].sqn = 1;
    tips.bsb_vec[1].digest = 1;
    ASSERT_EQ(tips.ComputeNumberAllRBs(), 4);

    tips.bsb_vec_new_epoch[1].sqn = 1;
    tips.bsb_vec_new_epoch[1].digest = 1;
    ASSERT_EQ(tips.ComputeNumberAllRBs(), 6);

    tips.bsb_vec[2].sqn = 1;
    ASSERT_EQ(tips.ComputeNumberAllRBs(), 6);

    tips.bsb_vec[3].epoch--;
    tips.bsb_vec[3].sqn = 1;
    tips.bsb_vec[3].digest = 1;
    ASSERT_EQ(tips.ComputeNumberAllRBs(), 6);

    tips.eb_tip_total_RBs = 10;
    ASSERT_EQ(tips.ComputeNumberAllRBs(), 16);
}

TEST (bootstrap, tip_set_compute_num_behind)
{
    uint32_t num_eb;
    uint32_t num_mb;
    uint64_t num_rb;

    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();

        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_eb, 0);
        ASSERT_EQ(num_mb, 0);
        ASSERT_EQ(num_rb, 0);
    }

    //other has 3 more eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();
        others.eb.epoch += 3;
        others.eb.digest = 1;
        others.mb.epoch += 3;
        others.mb.sqn += 3;
        others.mb.digest = 1;

        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_eb, 3);
        ASSERT_EQ(num_mb, 3);
        ASSERT_EQ(num_rb, 0);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 0;
            others.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 1;
            others.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 1;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2 * 2);
    }

    //other has 2 more eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();
        others.eb.epoch += 2;
        others.eb.digest = 1;
        others.mb.epoch += 2;
        others.mb.sqn += 2;
        others.mb.digest = 1;

        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_eb, 2);
        ASSERT_EQ(num_mb, 2);
        ASSERT_EQ(num_rb, 0);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 0;
            others.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 1;
            others.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 1;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2 * 2);
    }

    //other has 1 more eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();
        others.eb.epoch += 1;
        others.eb.digest = 1;
        others.mb.epoch += 1;
        others.mb.sqn += 1;
        others.mb.digest = 1;

        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_eb, 1);
        ASSERT_EQ(num_mb, 1);
        ASSERT_EQ(num_rb, 0);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 0;
            others.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 1;
            others.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 1;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2 * 2);

        //////////
        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec[i].epoch = my.eb.epoch + 1;
            my.bsb_vec[i].sqn = 1;
            my.bsb_vec[i].digest = 1;
        }
        others.eb_tip_total_RBs = NUM_DELEGATES * 2;
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 5 );

        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec_new_epoch[i].epoch = my.eb.epoch + 2;
            my.bsb_vec_new_epoch[i].sqn = 1;
            my.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 4 );
    }

    //same number of eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 0;
            others.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 1;
            others.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 1;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2);

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2 * 2);

        //////////
        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec[i].epoch = my.eb.epoch + 1;
            my.bsb_vec[i].sqn = 1;
            my.bsb_vec[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 3 );

        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec_new_epoch[i].epoch = my.eb.epoch + 2;
            my.bsb_vec_new_epoch[i].sqn = 1;
            my.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2 );

        ///////////
        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec[i].epoch = my.eb.epoch + 1;
            my.bsb_vec[i].sqn = 3;
            my.bsb_vec[i].digest = 1;
        }
        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec_new_epoch[i].epoch = my.eb.epoch + 2;
            my.bsb_vec_new_epoch[i].sqn = 3;
            my.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES * 2 );
    }

    //other has 1 less eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();

        my.eb.epoch += 1;
        my.eb.digest = 1;
        my.mb.epoch += 1;
        my.mb.sqn += 1;
        my.mb.digest = 1;

        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_eb, 0);
        ASSERT_EQ(num_mb, 0);
        ASSERT_EQ(num_rb, 0);

        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, NUM_DELEGATES );
    }

    //other has 2 less eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();

        my.eb.epoch += 2;
        my.eb.digest = 1;
        my.mb.epoch += 2;
        my.mb.sqn += 2;
        my.mb.digest = 1;

        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_eb, 0);
        ASSERT_EQ(num_mb, 0);
        ASSERT_EQ(num_rb, 0);

        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        my.eb_tip_total_RBs = NUM_DELEGATES;
        my.ComputeNumberBlocksBehind(others, num_eb, num_mb, num_rb);
        ASSERT_EQ(num_rb, 0);
    }
}

TEST (bootstrap, tip_set_valid)
{
    Bootstrap::TipSet others = create_tip_set();
    ASSERT_TRUE(others.ValidTips());

    others.mb.epoch = others.eb.epoch + 1;
    others.mb.sqn = 1;
    others.mb.digest = 1;
    ASSERT_TRUE(others.ValidTips());

    others.bsb_vec[0].epoch = others.eb.epoch + 2;
    ASSERT_FALSE(others.ValidTips());

    others.bsb_vec[0].epoch = others.eb.epoch + 1;
    ASSERT_TRUE(others.ValidTips());
    others.bsb_vec[0].epoch = others.mb.epoch + 1;
    ASSERT_FALSE(others.ValidTips());

    others.bsb_vec[0].epoch = others.mb.epoch;
    others.bsb_vec_new_epoch[0].epoch = others.mb.epoch + 1;
    ASSERT_TRUE(others.ValidTips());

    others.bsb_vec_new_epoch[0].epoch = others.mb.epoch + 2;
    ASSERT_FALSE(others.ValidTips());
    others.bsb_vec_new_epoch[0].epoch = others.mb.epoch + 1;

    others.eb.digest = 0;
    ASSERT_FALSE(others.ValidTips());

    others.eb.digest = 3;
    others.mb.digest = 0;
    ASSERT_FALSE(others.ValidTips());

    others.mb.digest = 3;
    others.bsb_vec[0].sqn = 1;
    others.bsb_vec[0].digest = 0;
    ASSERT_FALSE(others.ValidTips());

    others.bsb_vec[0].epoch = 3;
    others.bsb_vec[0].sqn = 1;
    others.bsb_vec[0].digest = 1;
    ASSERT_TRUE(others.ValidTips());

    others.bsb_vec_new_epoch[0].epoch = 3;
    others.bsb_vec_new_epoch[0].sqn = 0;
    others.bsb_vec_new_epoch[0].digest = 1;
    ASSERT_FALSE(others.ValidTips());
}

TEST (bootstrap, tip_set_valid_other)
{
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();
        ASSERT_TRUE(my.ValidPeerTips(others));

        my.bsb_vec[0].epoch = my.eb.epoch + 1;
        my.bsb_vec[0].sqn = 1;
        my.bsb_vec[0].digest = 1;
        others.mb.epoch = others.eb.epoch + 1;
        others.mb.sqn = 1;
        others.mb.digest = 1;
        ASSERT_TRUE(my.ValidPeerTips(others));

        others.bsb_vec[0].epoch = others.eb.epoch + 2;
        ASSERT_FALSE(my.ValidPeerTips(others));
        others.bsb_vec[0].epoch = others.eb.epoch + 1;

        others.eb_tip_total_RBs += 1;
        ASSERT_FALSE(my.ValidPeerTips(others));
    }

    //other has 2 more eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();
        others.eb.epoch += 2;
        others.eb.digest = 1;
        others.mb.epoch += 2;
        others.mb.sqn += 2;
        others.mb.digest = 1;
        ASSERT_TRUE(my.ValidPeerTips(others));

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 0;
            others.bsb_vec[i].digest = 1;
        }
        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        ASSERT_TRUE(my.ValidPeerTips(others));
        my.eb_tip_total_RBs += 1;
        ASSERT_FALSE(my.ValidPeerTips(others));
        my.eb_tip_total_RBs -= 1;

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            my.bsb_vec[i].epoch = my.eb.epoch + 1;
            my.bsb_vec[i].sqn = 10;
            my.bsb_vec[i].digest = 1;
        }
        ASSERT_FALSE(my.ValidPeerTips(others));
    }

    //other has 1 more eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();
        others.eb.epoch += 1;
        others.eb.digest = 1;
        others.mb.epoch += 1;
        others.mb.sqn += 1;
        others.mb.digest = 1;
        ASSERT_TRUE(my.ValidPeerTips(others));

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 0;
            others.bsb_vec[i].digest = 1;
        }
        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        ASSERT_TRUE(my.ValidPeerTips(others));

        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec_new_epoch[i].epoch = my.eb.epoch + 2;
            my.bsb_vec_new_epoch[i].sqn = 1;
            my.bsb_vec_new_epoch[i].digest = 1;
        }
        ASSERT_TRUE(my.ValidPeerTips(others));

        my.eb_tip_total_RBs += 1;
        ASSERT_FALSE(my.ValidPeerTips(others));
        my.eb_tip_total_RBs -= 1;

        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec[i].epoch = my.eb.epoch + 1;
            my.bsb_vec[i].sqn = 1;
            my.bsb_vec[i].digest = 1;
        }
        ASSERT_FALSE(my.ValidPeerTips(others));

        others.eb_tip_total_RBs += 100;
        ASSERT_TRUE(my.ValidPeerTips(others));

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            my.bsb_vec[i].epoch = my.eb.epoch + 1;
            my.bsb_vec[i].sqn = 10;
            my.bsb_vec[i].digest = 1;
        }
        ASSERT_FALSE(my.ValidPeerTips(others));
    }

    //same number of eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 0;
            others.bsb_vec[i].digest = 1;
        }
        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 1;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec[i].epoch = my.eb.epoch + 1;
            my.bsb_vec[i].sqn = 1;
            my.bsb_vec[i].digest = 1;
        }
        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            my.bsb_vec_new_epoch[i].epoch = my.eb.epoch + 2;
            my.bsb_vec_new_epoch[i].sqn = 1;
            my.bsb_vec_new_epoch[i].digest = 1;
        }
        ASSERT_TRUE(my.ValidPeerTips(others));

        my.eb_tip_total_RBs += 1;
        ASSERT_FALSE(my.ValidPeerTips(others));
    }

    //other has 1 less eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();

        my.eb.epoch += 1;
        my.eb.digest = 1;
        my.mb.epoch += 1;
        my.mb.sqn += 1;
        my.mb.digest = 1;

        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        ASSERT_TRUE(my.ValidPeerTips(others));

        for(uint i = 0; i < NUM_DELEGATES; ++i)
        {
            others.bsb_vec[i].epoch = others.eb.epoch + 1;
            others.bsb_vec[i].sqn = 0;
            others.bsb_vec[i].digest = 1;
        }
        ASSERT_FALSE(my.ValidPeerTips(others));

        my.eb_tip_total_RBs = 100;
        ASSERT_TRUE(my.ValidPeerTips(others));
    }

    //other has 2 less eb
    {
        Bootstrap::TipSet my = create_tip_set();
        Bootstrap::TipSet others = create_tip_set();

        my.eb.epoch += 2;
        my.eb.digest = 1;
        my.mb.epoch += 2;
        my.mb.sqn += 2;
        my.mb.digest = 1;
        ASSERT_TRUE(my.ValidPeerTips(others));

        for(uint i = 0; i < NUM_DELEGATES / 2; ++i)
        {
            others.bsb_vec_new_epoch[i].epoch = others.eb.epoch + 2;
            others.bsb_vec_new_epoch[i].sqn = 1;
            others.bsb_vec_new_epoch[i].digest = 1;
        }
        ASSERT_FALSE(my.ValidPeerTips(others));
    }
}

class UT_Cache: public logos::IBlockCache
{
public:
    virtual logos::IBlockCache::add_result AddEpochBlock(EBPtr block) override
    {
        return addeb ? logos::IBlockCache::add_result::OK : logos::IBlockCache::add_result::FAILED;
    }
    virtual logos::IBlockCache::add_result AddMicroBlock(MBPtr block) override
    {
        return addmb ? logos::IBlockCache::add_result::OK : logos::IBlockCache::add_result::FAILED;
    }
    virtual logos::IBlockCache::add_result AddRequestBlock(RBPtr block) override
    {
        return addbsb ? logos::IBlockCache::add_result::OK : logos::IBlockCache::add_result::FAILED;
    }
    virtual void StoreEpochBlock(EBPtr block) override
    {
    }
    virtual void StoreMicroBlock(MBPtr block) override
    {
    }
    virtual void StoreRequestBlock(RBPtr block) override
    {
    }
    virtual bool IsBlockCached(const BlockHash &b) override
    {
        return cached;
    }
    virtual bool IsBlockCachedOrQueued(const BlockHash &b) override
    {
        return cached;
    }

    bool addbsb = true;
    bool addmb = true;
    bool addeb = true;
    bool cached = false;
};

using PullPtr = std::shared_ptr<Bootstrap::PullRequest>;

TEST (bootstrap, puller)
{
    UT_Cache cache;
    boost::asio::io_service service;
    logos::alarm alarm (service);
    std::shared_ptr<Bootstrap::BootstrapAttempt> attempt;
    {
        Bootstrap::Puller puller(cache, alarm);
        ASSERT_EQ(puller.GetNumWaitingPulls(), 0);
        ASSERT_TRUE(puller.AllDone());
    }
    {
        Bootstrap::Puller puller(cache, alarm);
        Bootstrap::TipSet tips = create_tip_set();
        Bootstrap::TipSet tips_other = create_tip_set();
        tips_other.eb.epoch++;
        tips_other.eb.sqn++;
        puller.Init(attempt, tips, tips_other);
        ASSERT_EQ(puller.GetNumWaitingPulls(), 1);
    }
    {
        Bootstrap::Puller puller(cache, alarm);
        Bootstrap::TipSet tips = create_tip_set();
        Bootstrap::TipSet tips_other = create_tip_set();
        tips_other.mb.sqn++;
        puller.Init(attempt, tips, tips_other);
        ASSERT_EQ(puller.GetNumWaitingPulls(), 1);
    }
    {
        Bootstrap::Puller puller(cache, alarm);
        Bootstrap::TipSet tips = create_tip_set();
        Bootstrap::TipSet tips_other = create_tip_set();
        for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
            tips_other.bsb_vec[i].sqn++;
            tips_other.bsb_vec[i].digest = 1;
        }
        puller.Init(attempt, tips, tips_other);
        ASSERT_EQ(puller.GetNumWaitingPulls(), NUM_DELEGATES);
    }

    {
        Bootstrap::Puller puller(cache, alarm);
        Bootstrap::TipSet tips = create_tip_set();
        Bootstrap::TipSet tips_other = create_tip_set();
        for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
            tips_other.bsb_vec[i].sqn++;
            tips_other.bsb_vec[i].digest = 1;
        }

        puller.Init(attempt, tips, tips_other);
        std::vector<PullPtr> pulls;
        for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
            pulls.push_back(puller.GetPull());
        }
        ASSERT_EQ(puller.GetNumWaitingPulls(), 0);
        ASSERT_FALSE(puller.AllDone());

        for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
            puller.PullFailed(pulls[i]);
        }
        ASSERT_EQ(puller.GetNumWaitingPulls(), NUM_DELEGATES);

        PullPtr pull = puller.GetPull();
        auto bsb = std::make_shared<PostCommittedBlock<ConsensusType::Request>>();
        bsb->epoch_number = tips.bsb_vec.front().epoch;
        bsb->sequence = tips.bsb_vec.front().sqn+1;
        bsb->previous = pull->prev_hash;
        bsb->primary_delegate = 0;
        ASSERT_EQ(puller.BSBReceived(pull, bsb, false), Bootstrap::PullStatus::Continue);
    }
}
