#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <gtest/gtest.h>
#include <ed25519-donna/ed25519.h>
#include <vector>

#include <logos/node/common.hpp>
#include <logos/consensus/messages/messages.hpp>
#include <logos/consensus/messages/rejection.hpp>
#include <logos/unit_test/msg_validator_setup.hpp>

using namespace std;


TEST (crypto, ed25519)
{
	AccountPrivKey prv (0);
	AccountPubKey pub;
	ed25519_publickey (prv.data (), pub.data ());
	BlockHash message (1234567890);
	AccountSig signature;
	ed25519_sign (message.data (), HASH_SIZE, prv.data (), pub.data (), signature.data ());
	auto valid1 (ed25519_sign_open (message.data (), HASH_SIZE, pub.data (), signature.data ()));
	ASSERT_EQ (0, valid1);
	signature.data()[32] ^= 0x1;
	auto valid2 (ed25519_sign_open (message.data (), HASH_SIZE, pub.data (), signature.data ()));
	ASSERT_NE (0, valid2);
}

TEST (crypto, blake2b)
{
    struct HashData
    {
        uint8_t x;
        HashData(uint8_t x):x(x){}
        void Hash(blake2b_state & hash) const
        {
            blake2b_update(&hash, this, sizeof(HashData));
        }
    };

    auto a = Blake2bHash<HashData>(HashData(1));
    auto b = Blake2bHash<HashData>(HashData(1));
    auto c = Blake2bHash<HashData>(HashData(3));

    ASSERT_EQ(a, b);
    ASSERT_NE(b, c);
}

#if 0
TEST (crypto, bls)
{
    auto nodes = setup_nodes();
    SigVec sigs;
    BlockHash msg(123);

    //sign
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        MessageValidator::DelegateSignature sig;
        sig.delegate_id = i;
        validator.Sign(msg, sig.signature);
        sigs.push_back(sig);
    }
    //single verify
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        for(uint8_t j = 0; j < NUM_DELEGATES; ++j)
        {
            ASSERT_TRUE(validator.Validate(msg, sigs[j].signature, sigs[j].delegate_id));
        }
    }
    //aggregate
    uint8_t primary = 7;
    AggSignature agg_sig;
    nodes[primary]->validator.AggregateSignature(sigs, agg_sig);
    //verify
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        ASSERT_TRUE(validator.Validate(msg, agg_sig));
    }

    //error cases
    BlockHash wrong_msg(45); //wrong_msg
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        for(uint8_t j = 0; j < NUM_DELEGATES; ++j)
        {
            ASSERT_FALSE(validator.Validate(wrong_msg, sigs[j].signature, sigs[j].delegate_id));
        }
    }

    DelegateSig wrong_sig = 12; //wrong_sig
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        for(uint8_t j = 0; j < NUM_DELEGATES; ++j)
        {
            ASSERT_FALSE(validator.Validate(msg, wrong_sig, sigs[j].delegate_id));
        }
    }

    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        for(uint8_t j = 0; j < NUM_DELEGATES; ++j)
        {
            uint8_t wrong_id = sigs[j].delegate_id+1; //wrong_id
            ASSERT_FALSE(validator.Validate(msg, sigs[j].signature, wrong_id));
        }
    }

    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        ASSERT_FALSE(validator.Validate(wrong_msg, agg_sig)); //wrong_msg, agg_verify
    }

    AggSignature wrong_agg_sig = agg_sig;//wrong_agg_sig, agg_verify
    wrong_agg_sig.map.flip(3);
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        ASSERT_FALSE(validator.Validate(msg, wrong_agg_sig));
    }
}

TEST (write_read, all)
{
    BlockHash hash = 1;
    DelegateSig dsig = 2;
    DelegatePubKey dpub = 3;
    DelegatePrivKey dpriv = 4;
    AccountAddress aa = 5;
    AccountPubKey apub = 6;
    AccountPrivKey apriv = 7;
    AccountSig asig = 8;
    Amount amount = 9;
    uint64_t ui64 = 10;


    BlockHash hash2;
    DelegateSig dsig2;
    DelegatePubKey dpub2;
    DelegatePrivKey dpriv2;
    AccountAddress aa2;
    AccountPubKey apub2;
    AccountPrivKey apriv2;
    AccountSig asig2;
    Amount amount2;
    uint64_t ui642;

    std::vector<uint8_t> buf;
    {
        logos::vectorstream write_stream(buf);
        logos::write(write_stream, hash);
        logos::write(write_stream, dsig);
        logos::write(write_stream, dpub);
        logos::write(write_stream, dpriv);
        logos::write(write_stream, aa);
        logos::write(write_stream, apub);
        logos::write(write_stream, apriv);
        logos::write(write_stream, asig);
        logos::write(write_stream, amount);
        logos::write(write_stream, ui64);
    }
    //std::cout << "buf.size=" << buf.size() << std::endl;

    logos::bufferstream read_stream(buf.data(), buf.size());
    logos::read(read_stream, hash2);
    logos::read(read_stream, dsig2);
    logos::read(read_stream, dpub2);
    logos::read(read_stream, dpriv2);
    logos::read(read_stream, aa2);
    logos::read(read_stream, apub2);
    logos::read(read_stream, apriv2);
    logos::read(read_stream, asig2);
    logos::read(read_stream, amount2);
    logos::read(read_stream, ui642);

    ASSERT_EQ(hash, hash2);
    ASSERT_EQ(dsig, dsig2);
    ASSERT_EQ(dpub, dpub2);
    ASSERT_EQ(dpriv, dpriv2);
    ASSERT_EQ(aa, aa2);
    ASSERT_EQ(apub, apub2);
    ASSERT_EQ(apriv, apriv2);
    ASSERT_EQ(asig, asig2);
    ASSERT_EQ(amount, amount2);
    ASSERT_EQ(ui64, ui642);
}
#endif

TEST (messages, KeyAdvertisement)
{
    KeyAdvertisement block;
    block.public_key = 123;
    vector<uint8_t> buf;
    block.Serialize(buf);
    //cout << "block.payload_size=" << block.payload_size << endl;

    bool error = false;
    logos::bufferstream stream(buf.data(), buf.size());

    Prequel prequel(error, stream);
    ASSERT_EQ(block.payload_size, prequel.payload_size);
    //cout << "prequel.payload_size=" << prequel.payload_size << endl;

    KeyAdvertisement block2(error, stream, prequel.version);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.public_key, block2.public_key);
    ASSERT_EQ(block.version, block2.version);
    ASSERT_EQ(block.type, block2.type);
    ASSERT_EQ(block.consensus_type, block2.consensus_type);
}

TEST (messages, HeartBeat)
{
    for(int i = 0; i < 10000; ++i)
    {
        HeartBeat block;
        vector<uint8_t> buf;
        block.Serialize(buf);
        //cout << "block.payload_size=" << block.payload_size << endl;

        bool error = false;
        logos::bufferstream stream(buf.data(), buf.size());
        Prequel prequel(error, stream);
        HeartBeat block2(error, stream, prequel.version);
        //cout << "prequel.payload_size=" << prequel.payload_size << endl;
        ASSERT_EQ(block.payload_size, prequel.payload_size);

        ASSERT_EQ(prequel.payload_size, sizeof(block.is_request));
        ASSERT_EQ(prequel.type, MessageType::Heart_Beat);
        ASSERT_EQ(prequel.consensus_type, ConsensusType::Any);
        ASSERT_EQ(prequel.version, logos_version);

        ASSERT_FALSE(error);
        ASSERT_EQ(block.is_request, block2.is_request);
        ASSERT_EQ(block.version, block2.version);
        ASSERT_EQ(block.type, block2.type);
        ASSERT_EQ(block.consensus_type, block2.consensus_type);
    }
}

TEST (messages, ConnectedClientIds)
{
    char ip[INET6_ADDRSTRLEN] = {'c'};
    ConnectedClientIds block(1u,2u,EpochConnection::Transitioning, ip);
    vector<uint8_t> buf;
    block.Serialize(buf);

    bool error = false;
    logos::bufferstream stream(buf.data(), buf.size());
    ConnectedClientIds block2(error, stream);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.epoch_number, block2.epoch_number);
    ASSERT_EQ(block.delegate_id, block2.delegate_id);
    ASSERT_EQ(block.connection, block2.connection);
    ASSERT_EQ(memcmp(block.ip, block2.ip, INET6_ADDRSTRLEN), 0);
}

TEST (messages, StandardPhaseMessage)
{
    PrepareMessage<ConsensusType::Epoch> block(23);
    block.signature = 45;

    vector<uint8_t> buf;
    block.Serialize(buf);

    bool error = false;
    logos::bufferstream stream(buf.data(), buf.size());
    Prequel prequel(error, stream);
    PrepareMessage<ConsensusType::Epoch> block2(error, stream, prequel.version);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.preprepare_hash, block2.preprepare_hash);
    ASSERT_EQ(block.signature, block2.signature);
    ASSERT_EQ(block.version, block2.version);
    ASSERT_EQ(block.type, block2.type);
    ASSERT_EQ(block.consensus_type, block2.consensus_type);
}

TEST (messages, PostPhaseMessage)
{
    BlockHash pp_hash = 11;
    AggSignature as;
    as.map = 12;
    as.sig = 34;
    PostPrepareMessage<ConsensusType::Epoch> block(pp_hash, as);

    vector<uint8_t> buf;
    block.Serialize(buf);

    bool error = false;
    logos::bufferstream stream(buf.data(), buf.size());
    Prequel prequel(error, stream);
    ASSERT_EQ(prequel.version, block.version);
    ASSERT_EQ(prequel.type, block.type);
    ASSERT_EQ(prequel.consensus_type, block.consensus_type);

    PostPrepareMessage<ConsensusType::Epoch> block2(error, stream, prequel.version);
    ASSERT_EQ(block.preprepare_hash, block2.preprepare_hash);
    ASSERT_EQ(block.signature.map, block2.signature.map);
    ASSERT_EQ(block.signature.sig, block2.signature.sig);
}

TEST (messages, RejectionMessage)
{
    BlockHash pp_hash = 11;
    RejectionMessage<ConsensusType::BatchStateBlock> block(pp_hash);
    block.reason = RejectionReason::Bad_Signature;
    for(uint16_t i = 0; i < CONSENSUS_BATCH_SIZE; i+=2)
    {
        block.rejection_map.flip(i);
    }
    block.signature = 123;
    vector<uint8_t> buf;
    block.Serialize(buf);

    bool error = false;
    logos::bufferstream stream(buf.data(), buf.size());
    Prequel prequel(error, stream);
    ASSERT_EQ(prequel.version, block.version);
    ASSERT_EQ(prequel.type, block.type);
    ASSERT_EQ(prequel.consensus_type, block.consensus_type);

    RejectionMessage<ConsensusType::BatchStateBlock> block2(error, stream, prequel.version);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

TEST (blocks, receive_block)
{
    ReceiveBlock block(1,2,3);
    auto r_hash = block.Hash();

    std::vector<uint8_t> buf;
    auto db_val = block.to_mdb_val(buf);
    bool error = false;
    ReceiveBlock block2(error, db_val);
    auto r2_hash = block2.Hash();

    ASSERT_FALSE(error);
    ASSERT_EQ(r_hash, r2_hash);
}

TEST (blocks, state_block)
{
    StateBlock block(1,2,3,StateBlock::Type::send,5,6,7,8,9);

    std::vector<uint8_t> buf;
    auto db_val = block.to_mdb_val(buf);

    bool error = false;
    StateBlock block2(error, db_val);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.GetHash(), block2.GetHash());
    ASSERT_EQ(block.Hash(), block2.Hash());
    ASSERT_EQ(block.Hash(), block.GetHash());
    ASSERT_EQ(block2.Hash(), block2.GetHash());
}


void create_real_StateBlock(StateBlock & block)
{
    logos::keypair pair(std::string("34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4"));
    Amount amount(std::numeric_limits<logos::uint128_t>::max());
    Amount fee(0);
    uint64_t work = 0;

    AccountAddress account = pair.pub;
    AccountPubKey pub_key = pair.pub;
    AccountPrivKey priv_key = pair.prv.data;

    new (&block) StateBlock(account,  // account
            BlockHash(), // previous
            0, // sqn
            StateBlock::Type::send, //Type
            account,  // target
            amount,
            fee,
            priv_key,
            pub_key,
            work);
}

TEST (blocks, state_block_json)
{
    StateBlock block;
    create_real_StateBlock(block);
    auto s(block.SerializeJson(true, true));

    // std::cout << "StateBlock1 json: " << s << std::endl;

    bool error = false;
    boost::property_tree::ptree tree;
    std::stringstream istream(s);
    boost::property_tree::read_json(istream, tree);
    StateBlock block2(error, tree, true, true);
    auto s2(block2.SerializeJson(true, true));

    //    std::cout << "StateBlock2 json: " << s2 << std::endl;

    ASSERT_FALSE(error);
    ASSERT_EQ(block.GetHash(), block2.GetHash());
    ASSERT_EQ(block.Hash(), block2.Hash());
    ASSERT_EQ(block.Hash(), block.GetHash());
    ASSERT_EQ(block2.Hash(), block2.GetHash());
}

PrePrepareMessage<ConsensusType::BatchStateBlock>
create_bsb_preprepare(uint16_t num_sb)
{
    PrePrepareMessage<ConsensusType::BatchStateBlock> block;
    for(uint32_t i = 0; i < num_sb; ++i)
    {
        block.AddStateBlock(StateBlock(1,2,i,StateBlock::Type::send,5,6,7,8,9));
    }

    return block;
}

TEST (blocks, batch_state_block_PrePrepare_empty)
{
    auto block = create_bsb_preprepare(0);
    vector<uint8_t> buf;
    block.Serialize(buf);

    bool error = false;
    logos::bufferstream stream(buf.data()+MessagePrequelSize, buf.size()-MessagePrequelSize);
    PrePrepareMessage<ConsensusType::BatchStateBlock> block2(error, stream, logos_version);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

TEST (blocks, batch_state_block_PrePrepare_full)
{
    auto block = create_bsb_preprepare(CONSENSUS_BATCH_SIZE);
    ASSERT_FALSE(block.AddStateBlock(StateBlock(1,2,CONSENSUS_BATCH_SIZE+1,StateBlock::Type::send,5,6,7,8,9)));
    ASSERT_EQ(block.block_count, CONSENSUS_BATCH_SIZE);
    vector<uint8_t> buf;
    block.Serialize(buf);

    bool error = false;
    logos::bufferstream stream(buf.data()+MessagePrequelSize, buf.size()-MessagePrequelSize);
    PrePrepareMessage<ConsensusType::BatchStateBlock> block2(error, stream, logos_version);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

template<ConsensusType CT>
PostCommittedBlock<CT> create_approved_block(PrePrepareMessage<CT> & preperpare)
{
    AggSignature    post_prepare_sig;
    AggSignature    post_commit_sig;

    post_prepare_sig.map = 12;
    post_prepare_sig.sig = 34;
    post_commit_sig.map = 56;
    post_commit_sig.sig = 78;

    return PostCommittedBlock<CT>(preperpare, post_prepare_sig, post_commit_sig);
}

TEST (blocks, batch_state_block_PostCommit_net)
{
    auto block_pp = create_bsb_preprepare(CONSENSUS_BATCH_SIZE/3);/* /3 so not a full block*/
    auto block = create_approved_block<ConsensusType::BatchStateBlock>(block_pp);

    vector<uint8_t> buf;
    block.Serialize(buf, true, false);

    bool error = false;
    logos::bufferstream stream(buf.data()+MessagePrequelSize, buf.size()-MessagePrequelSize);
    PostCommittedBlock<ConsensusType::BatchStateBlock> block2(error, stream, logos_version, true, false);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

TEST (blocks, batch_state_block_PostCommit_DB)
{
    auto block_pp = create_bsb_preprepare(CONSENSUS_BATCH_SIZE/2);/* /2 so not a full block*/
    auto block = create_approved_block<ConsensusType::BatchStateBlock>(block_pp);
    block.next = 90;

    vector<uint8_t> buf;
    auto block_db_val = block.to_mdb_val(buf);
    vector<vector<uint8_t> > buffers(CONSENSUS_BATCH_SIZE);
    vector<logos::mdb_val> sb_db_vals;
    for(uint16_t i = 0; i < CONSENSUS_BATCH_SIZE; ++i)
    {
        sb_db_vals.push_back(block.blocks[i].to_mdb_val(buffers[i]));
    }

    bool error = false;
    PostCommittedBlock<ConsensusType::BatchStateBlock> block2(error, block_db_val);
    ASSERT_FALSE(error);
    if(! error)
    {
        for(uint16_t i = 0; i < block.block_count; ++i)
        {
            new (&block.blocks[i]) StateBlock(error, sb_db_vals[i]);
            ASSERT_FALSE(error);
        }
    }

    ASSERT_EQ(block.Hash(), block2.Hash());
}

/////////////////////////////
PrePrepareMessage<ConsensusType::MicroBlock> create_mb_preprepare()
{
    PrePrepareMessage<ConsensusType::MicroBlock> block;
    block.last_micro_block = 1;
    block.number_batch_blocks = 2;
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        block.tips[i] = i;
    }
    return block;
}

TEST (blocks, micro_block_PrePrepare)
{
    auto block = create_mb_preprepare();
    vector<uint8_t> buf;
    block.Serialize(buf);

    bool error = false;
    logos::bufferstream stream(buf.data()+MessagePrequelSize, buf.size()-MessagePrequelSize);
    PrePrepareMessage<ConsensusType::MicroBlock> block2(error, stream, logos_version);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

TEST (blocks, micro_block_PostCommit_net)
{
    auto block_pp = create_mb_preprepare();
    auto block = create_approved_block<ConsensusType::MicroBlock>(block_pp);

    vector<uint8_t> buf;
    block.Serialize(buf, true, false);

    bool error = false;
    logos::bufferstream stream(buf.data()+MessagePrequelSize, buf.size()-MessagePrequelSize);
    PostCommittedBlock<ConsensusType::MicroBlock> block2(error, stream, logos_version, true, false);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

TEST (blocks, micro_block_PostCommit_DB)
{
    auto block_pp = create_mb_preprepare();
    auto block = create_approved_block<ConsensusType::MicroBlock>(block_pp);
    block.next = 90;

    vector<uint8_t> buf;
    auto block_db_val = block.to_mdb_val(buf);
    bool error = false;
    PostCommittedBlock<ConsensusType::MicroBlock> block2(error, block_db_val);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

////////////////////////////////
PrePrepareMessage<ConsensusType::Epoch> create_eb_preprepare()
{
    PrePrepareMessage<ConsensusType::Epoch> block;
    block.micro_block_tip = 1234;
    block.transaction_fee_pool = 2345;
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        block.delegates[i] = {i, i, i, i};
    }
    return block;
}

TEST (blocks, epoch_block_PrePrepare)
{
    auto block = create_eb_preprepare();

    vector<uint8_t> buf;
    block.Serialize(buf);

    bool error = false;
    logos::bufferstream stream(buf.data()+MessagePrequelSize, buf.size()-MessagePrequelSize);
    PrePrepareMessage<ConsensusType::Epoch> block2(error, stream, logos_version);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

TEST (blocks, epoch_block_PostCommit_net)
{
    auto block_pp = create_eb_preprepare();
    auto block = create_approved_block<ConsensusType::Epoch>(block_pp);

    vector<uint8_t> buf;
    block.Serialize(buf, true, false);

    bool error = false;
    logos::bufferstream stream(buf.data()+MessagePrequelSize, buf.size()-MessagePrequelSize);
    PostCommittedBlock<ConsensusType::Epoch> block2(error, stream, logos_version, true, false);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

TEST (blocks, epoch_block_PostCommit_DB)
{
    auto block_pp = create_eb_preprepare();
    auto block = create_approved_block<ConsensusType::Epoch>(block_pp);
    block.next = 90;

    vector<uint8_t> buf;
    auto block_db_val = block.to_mdb_val(buf);
    bool error = false;
    PostCommittedBlock<ConsensusType::Epoch> block2(error, block_db_val);

    ASSERT_FALSE(error);
    ASSERT_EQ(block.Hash(), block2.Hash());
}

#if 0
TEST (message_validator, consensus_session)
{
    auto nodes = setup_nodes();

    //step 1, pre-prepare
    //primary, node[0], signs the preprepare
    PrePrepareMessage<ConsensusType::Epoch> preprepare;
    preprepare.micro_block_tip = 1234;
    preprepare.transaction_fee_pool = 2345;
    for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
    {
        preprepare.delegates[i] = {i, i, i, i};
    }

    auto & primary = nodes[0]->validator;
    auto preprepare_hash = preprepare.Hash();
    primary.Sign(preprepare_hash, preprepare.preprepare_sig);
    ASSERT_TRUE(primary.Validate(preprepare_hash, preprepare.preprepare_sig, 0));
    std::vector<uint8_t> preprepare_buf;
    preprepare.Serialize(preprepare_buf);

    //step 2, prepare
    //backups (and primary)
    //verify the signature of preprepare and create signed prepares
    std::vector<std::vector<uint8_t>> prepare_bufs;
    std::vector<PrePrepareMessage<ConsensusType::Epoch>> preprepare_copies;
    for(int i = 0; i < NUM_DELEGATES; ++i)
    {
        bool error = false;
        logos::bufferstream stream(preprepare_buf.data()+MessagePrequelSize, preprepare_buf.size()-MessagePrequelSize);
        PrePrepareMessage<ConsensusType::Epoch> block2(error, stream, logos_version);
        ASSERT_FALSE(error);

        auto & validator = nodes[i]->validator;
        auto pre_prepare_hash = block2.Hash();
        ASSERT_EQ(pre_prepare_hash, preprepare_hash);
        ASSERT_TRUE(validator.Validate(pre_prepare_hash, block2.preprepare_sig, 0));
        preprepare_copies.push_back(block2);

        PrepareMessage<ConsensusType::Epoch> prepare(pre_prepare_hash);
        validator.Sign(pre_prepare_hash, prepare.signature);
        ASSERT_TRUE(validator.Validate(pre_prepare_hash, prepare.signature, i));
        prepare_bufs.push_back(std::vector<uint8_t>());
        auto & buf = prepare_bufs.back();
        prepare.Serialize(buf);
    }

    //step 3, post-prepare
    //primary verify the prepares and aggregate the signatures
    AggSignature postprepare_agg_sig;
    {
        SigVec signatures(NUM_DELEGATES);
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            //            auto & msg = prepares[i];
            //            ASSERT_TRUE(primary.Validate(preprepare_hash, msg.signature, i));
            //            signatures[i].delegate_id = i;
            //            signatures[i].signature = msg.signature;
            bool error = false;
            logos::bufferstream stream(prepare_bufs[i].data()+MessagePrequelSize, prepare_bufs[i].size()-MessagePrequelSize);
            PrepareMessage<ConsensusType::Epoch> prepare(error, stream, logos_version);
            ASSERT_FALSE(error);

            ASSERT_TRUE(primary.Validate(preprepare_hash, prepare.signature, i));
            signatures[i].delegate_id = i;
            signatures[i].signature = prepare.signature;
        }
        ASSERT_TRUE(primary.AggregateSignature(signatures, postprepare_agg_sig));
        ASSERT_TRUE(primary.Validate(preprepare_hash, postprepare_agg_sig));
    }
    PostPrepareMessage<ConsensusType::Epoch> postprepare(preprepare_hash, postprepare_agg_sig);
    auto postprepare_hash = postprepare.ComputeHash();
    std::vector<uint8_t> postprepare_buf;
    postprepare.Serialize(postprepare_buf);

    //step 4, commit
    //delegates verify the signature of postprepare and create signed commits
    //std::vector<CommitMessage<ConsensusType::Epoch>> commits;
    std::vector<std::vector<uint8_t>> commit_bufs;
    std::vector<AggSignature> postprepare_sig_copies;
    for(int i = 0; i < NUM_DELEGATES; ++i)
    {
        bool error = false;
        logos::bufferstream stream(postprepare_buf.data()+MessagePrequelSize, postprepare_buf.size()-MessagePrequelSize);
        PostPrepareMessage<ConsensusType::Epoch> block2(error, stream, logos_version);
        ASSERT_FALSE(error);

        auto & validator = nodes[i]->validator;
        ASSERT_TRUE(validator.Validate(preprepare_hash, block2.signature));
        auto post_prepare_hash = block2.ComputeHash();
        ASSERT_EQ(post_prepare_hash, postprepare_hash);
        postprepare_sig_copies.push_back(block2.signature);

        CommitMessage<ConsensusType::Epoch> commit(preprepare_hash);
        validator.Sign(post_prepare_hash, commit.signature);
        ASSERT_TRUE(validator.Validate(post_prepare_hash, commit.signature, i));

        commit_bufs.push_back(std::vector<uint8_t>());
        auto & buf = commit_bufs.back();
        commit.Serialize(buf);
    }

    //step 5, primary post-commit
    //primary verify the commits and aggregate the signatures
    AggSignature postcommit_agg_sig;
    {
        SigVec signatures(NUM_DELEGATES);
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            bool error = false;
            logos::bufferstream stream(commit_bufs[i].data()+MessagePrequelSize, commit_bufs[i].size()-MessagePrequelSize);
            CommitMessage<ConsensusType::Epoch> commit(error, stream, logos_version);
            ASSERT_FALSE(error);

            ASSERT_TRUE(primary.Validate(postprepare_hash, commit.signature, i));
            signatures[i].delegate_id = i;
            signatures[i].signature = commit.signature;
        }
        ASSERT_TRUE(primary.AggregateSignature(signatures, postcommit_agg_sig));
        ASSERT_TRUE(primary.Validate(postprepare_hash, postcommit_agg_sig));
    }
    PostCommitMessage<ConsensusType::Epoch> postcommit(preprepare_hash, postcommit_agg_sig);
    std::vector<uint8_t> postcommit_buf;
    postcommit.Serialize(postcommit_buf);
    PostCommittedBlock<ConsensusType::Epoch> primary_block(preprepare, postprepare_agg_sig, postcommit_agg_sig);

    //make sure hash matches
    ASSERT_EQ(preprepare_hash, primary_block.Hash());

    //step 6, backup post-commit
    //delegates verify the signature of postcommit
    for(int i = 0; i < NUM_DELEGATES; ++i)
    {
        bool error = false;
        logos::bufferstream stream(postcommit_buf.data()+MessagePrequelSize, postcommit_buf.size()-MessagePrequelSize);
        PostCommitMessage<ConsensusType::Epoch> block2(error, stream, logos_version);
        ASSERT_FALSE(error);

        auto & validator = nodes[i]->validator;
        ASSERT_TRUE(validator.Validate(postprepare_hash, block2.signature));
        PostCommittedBlock<ConsensusType::Epoch> backup_block(preprepare_copies[i], postprepare_sig_copies[i], block2.signature);

        ASSERT_EQ(primary_block.Hash(), backup_block.Hash());
        ASSERT_EQ(primary_block.post_prepare_sig.map, backup_block.post_prepare_sig.map);
        ASSERT_EQ(primary_block.post_prepare_sig.sig, backup_block.post_prepare_sig.sig);
        ASSERT_EQ(primary_block.post_commit_sig.map, backup_block.post_commit_sig.map);
        ASSERT_EQ(primary_block.post_commit_sig.sig, backup_block.post_commit_sig.sig);
    }
}

#define LOOPS   10
TEST (message_validator, signature_order_twoThirds)
{
    auto nodes = setup_nodes();

    PrePrepareMessage<ConsensusType::Epoch> preperpare;

    auto & primary = nodes[0]->validator;
    auto preprepare_hash = preperpare.Hash();
    primary.Sign(preprepare_hash, preperpare.preprepare_sig);
    ASSERT_TRUE(primary.Validate(preprepare_hash, preperpare.preprepare_sig, 0));

    std::vector<PrepareMessage<ConsensusType::Epoch>> prepares;
    for(int i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i]->validator;
        ASSERT_TRUE(validator.Validate(preprepare_hash, preperpare.preprepare_sig, 0));

        prepares.push_back(PrepareMessage<ConsensusType::Epoch>(preprepare_hash));
        auto & msg = prepares.back();
        validator.Sign(preprepare_hash, msg.signature);
        ASSERT_TRUE(validator.Validate(preprepare_hash, msg.signature, i));
    }

    for(int r = 0; r < LOOPS; ++r)
    {
        //Now the primary have prepares. It aggregates 2/3*32 signatures with random order.

        AggSignature postprepare_agg_sig;
        {
            SigVec signatures(NUM_DELEGATES);
            for(int i = 0; i < NUM_DELEGATES; ++i)
            {
                auto & msg = prepares[i];
                ASSERT_TRUE(primary.Validate(preprepare_hash, msg.signature, i));
                signatures[i].delegate_id = i;
                signatures[i].signature = msg.signature;
            }

            std::random_shuffle ( signatures.begin(), signatures.end() );
            signatures.resize(NUM_DELEGATES*2/3);

            SigVec signatures_copy;
            for(auto &xx : signatures)
            {
                signatures_copy.push_back(xx);
                signatures_copy.push_back(xx);
            }

            ASSERT_TRUE(primary.AggregateSignature(signatures_copy, postprepare_agg_sig));
            ASSERT_TRUE(primary.Validate(preprepare_hash, postprepare_agg_sig));
        }
        PostPrepareMessage<ConsensusType::Epoch> postprepare(preprepare_hash, postprepare_agg_sig);

        //All delegate verify the aggregated signature
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            auto & validator = nodes[i]->validator;
            ASSERT_TRUE(validator.Validate(preprepare_hash, postprepare.signature));
        }
    }
}
#endif

TEST (DB, bsb)
{
    auto store = get_db();
    ASSERT_TRUE(store != NULL);
    if(store == NULL)
        return;
    logos::transaction txn(store->environment, nullptr, true);

    auto block_pp = create_bsb_preprepare(CONSENSUS_BATCH_SIZE/2);
    auto block = create_approved_block<ConsensusType::BatchStateBlock>(block_pp);
    block.next = 90;

    auto block_hash(block.Hash());
    ASSERT_FALSE(store->batch_block_put(block, block_hash, txn));

    ApprovedBSB block2;
    ASSERT_FALSE(store->batch_block_get(block_hash, block2, txn));
    ASSERT_EQ(block_hash, block2.Hash());
}

TEST (DB, mb)
{
    auto store = get_db();
    ASSERT_TRUE(store != NULL);
    if(store == NULL)
        return;

    logos::transaction txn(store->environment, nullptr, true);
    auto block_pp = create_mb_preprepare();
    auto block = create_approved_block<ConsensusType::MicroBlock>(block_pp);
    block.next = 90;

    ASSERT_FALSE(store->micro_block_put(block, txn));
    auto block_hash(block.Hash());

    ApprovedMB block2;
    ASSERT_FALSE(store->micro_block_get(block_hash, block2, txn));
    ASSERT_EQ(block_hash, block2.Hash());
}

TEST (DB, eb)
{
    auto store = get_db();
    ASSERT_TRUE(store != NULL);
    if(store == NULL)
        return;

    logos::transaction txn(store->environment, nullptr, true);
    auto block_pp = create_eb_preprepare();
    auto block = create_approved_block<ConsensusType::Epoch>(block_pp);
    block.next = 90;

    ASSERT_FALSE(store->epoch_put(block, txn));
    auto block_hash(block.Hash());

    ApprovedEB block2;
    ASSERT_FALSE(store->epoch_get(block_hash, block2, txn));
    ASSERT_EQ(block_hash, block2.Hash());
}

