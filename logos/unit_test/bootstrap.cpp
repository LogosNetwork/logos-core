#include <gtest/gtest.h>
#include <vector>

#include <logos/bootstrap/bootstrap_messages.hpp>
#include <logos/bootstrap/pull.hpp>
#include <logos/node/common.hpp>
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
			123), header2;

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
        Bootstrap::MessageHeader(error, read_stream);
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


static TipSet create_tip_set()
{
	TipSet tips;
	uint32_t epoch_num = 1;
	uint32_t mb_sqn = 1;

	tips.eb = {epoch_num, epoch_num, 3};
	tips.mb = {epoch_num+1, mb_sqn, 4};

	for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
		tips.bsb_vec[i] = {epoch_num+1, 0, 0};
	}
	for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
		tips.bsb_vec_new_epoch[i] = {epoch_num+2, 0, 0};
	}
	return tips;
}

TEST (bootstrap, msg_tip_set)
{
	TipSet tips = create_tip_set();

    std::vector<uint8_t> buf;
    {
        logos::vectorstream write_stream(buf);
        tips.Serialize(write_stream);
    }
    std::cout << "buf.size=" << buf.size() << std::endl;
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size());
        TipSet tips2(error, read_stream);
        ASSERT_FALSE(error);
        ASSERT_EQ(tips, tips2);
    }
    {
        bool error = false;
        logos::bufferstream read_stream(buf.data(), buf.size()-1);
        TipSet tips2(error, read_stream);
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

class UT_Cache: public IBlockCache
{
public:
    virtual bool AddEB(EBPtr block) override
    {
    	return addeb;
    }
    virtual bool AddMB(MBPtr block) override
    {
    	return addmb;
    }
    virtual bool AddBSB(BSBPtr block) override
    {
    	return addbsb;
    }
    virtual bool IsBlockCached(const BlockHash &b) override
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
	{
		Bootstrap::Puller puller(cache);
		ASSERT_EQ(puller.GetNumWaitingPulls(), 0);
		ASSERT_TRUE(puller.AllDone());
	}
	{
		Bootstrap::Puller puller(cache);
		TipSet tips = create_tip_set();
		TipSet tips_other = create_tip_set();
		tips_other.eb.epoch++;
		tips_other.eb.sqn++;
		puller.Init(tips, tips_other);
		ASSERT_EQ(puller.GetNumWaitingPulls(), 1);
	}
	{
		Bootstrap::Puller puller(cache);
		TipSet tips = create_tip_set();
		TipSet tips_other = create_tip_set();
		tips_other.mb.sqn++;
		puller.Init(tips, tips_other);
		ASSERT_EQ(puller.GetNumWaitingPulls(), 1);
	}
	{
		Bootstrap::Puller puller(cache);
		TipSet tips = create_tip_set();
		TipSet tips_other = create_tip_set();
		for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
			tips_other.bsb_vec[i].sqn++;
		}
		puller.Init(tips, tips_other);
		ASSERT_EQ(puller.GetNumWaitingPulls(), NUM_DELEGATES);
	}

	{
		Bootstrap::Puller puller(cache);
		TipSet tips = create_tip_set();
		TipSet tips_other = create_tip_set();
		for (uint32_t i = 0; i < NUM_DELEGATES; ++i) {
			tips_other.bsb_vec[i].sqn++;
		}

		puller.Init(tips, tips_other);
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
