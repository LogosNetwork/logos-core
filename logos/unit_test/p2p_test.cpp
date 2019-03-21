#include <gtest/gtest.h>

#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../consensus/p2p/consensus_p2p.hpp"

#define TEST_DIR  ".logos_test"
#define TEST_DB   TEST_DIR "/data.ldb"

TEST (P2pTest, VerifyPeersInterface)
{
    const char *argv[] = {"unit_test", "-debug=net", 0};
    const char *peers[] = {"230.1.0.129:12345", "63.15.7.3:65535", "8.8.8.8:8888", "8.8.8.9:8888", "4.4.4.4:14495", "230.1.0.129:12346"};
    constexpr int npeers = sizeof(peers) / sizeof(char *);
    p2p_config config;

    config.argc = 2;
    config.argv = (char **)argv;
    config.boost_io_service = 0;
    config.test_mode = true;

    config.scheduleAfterMs = [](std::function<void()> const &handler, unsigned ms)
    {
        printf("scheduleAfterMs called.\n");
    };

    config.userInterfaceMessage = [](int type, const char *mess)
    {
        printf("%s%s: %s\n", (type & P2P_UI_INIT ? "init " : ""),
            (type & P2P_UI_ERROR ? "error" : type & P2P_UI_WARNING ? "warning" : "message"), mess);
    };

    system("rm -rf " TEST_DIR "; mkdir " TEST_DIR);

    for (int i = 0; i < 2; ++i)
    {
        p2p_interface p2p;
        bool error = false;
        boost::filesystem::path const data_path(TEST_DB);
        logos::block_store store(error, data_path, 32);
        EXPECT_EQ(error, false);
        ContainerP2p cp2p(p2p, store);

        config.lmdb_env = store.environment.environment;
        config.lmdb_dbi = store.p2p_db;

        EXPECT_EQ(p2p.Init(config), true);

        if (i == 0)
        {
            EXPECT_EQ(p2p.load_databases(), false);
            cp2p.add_to_blacklist(logos::endpoint(boost::asio::ip::address::from_string("8.8.8.8"), 0));
            p2p.add_peers((char **)peers, 2);
            cp2p.add_to_blacklist(logos::endpoint(boost::asio::ip::address::from_string("230.1.0.129"), 0));
            p2p.add_peers((char **)peers + 2, 1);
            p2p.add_peers((char **)peers + 3, npeers - 3);
        }
        else
        {
            EXPECT_EQ(p2p.load_databases(), true);
        }

        EXPECT_EQ(cp2p.is_blacklisted(logos::endpoint(boost::asio::ip::address::from_string("4.4.4.4"), 0)), false);
        EXPECT_EQ(cp2p.is_blacklisted(logos::endpoint(boost::asio::ip::address::from_string("8.8.8.8"), 0)), true);
        EXPECT_EQ(cp2p.is_blacklisted(logos::endpoint(boost::asio::ip::address::from_string("8.8.8.9"), 0)), false);
        EXPECT_EQ(cp2p.is_blacklisted(logos::endpoint(boost::asio::ip::address::from_string("8.128.8.8"), 0)), false);
        EXPECT_EQ(cp2p.is_blacklisted(logos::endpoint(boost::asio::ip::address::from_string("230.0.0.129"), 0)), false);
        EXPECT_EQ(cp2p.is_blacklisted(logos::endpoint(boost::asio::ip::address::from_string("230.1.0.129"), 0)), true);
        EXPECT_EQ(cp2p.is_blacklisted(logos::endpoint(boost::asio::ip::address::from_string("255.1.0.129"), 0)), false);

        int id1 = P2P_GET_PEER_NEW_SESSION, id2 = P2P_GET_PEER_NEW_SESSION;
        vector<logos::endpoint> nodes1, nodes2;

        id1 = cp2p.get_peers(id1, nodes1, 1);
        EXPECT_EQ(nodes1.size(), 1);
        id2 = cp2p.get_peers(id2, nodes2, 2);
        EXPECT_EQ(nodes2.size(), 2);
        id1 = cp2p.get_peers(id1, nodes1, 2);
        EXPECT_EQ(nodes1.size(), 3);
        id2 = cp2p.get_peers(id2, nodes2, 1);
        EXPECT_EQ(nodes2.size(), 3);
        id1 = cp2p.get_peers(id1, nodes1, npeers - 4);
        EXPECT_EQ(nodes1.size(), npeers - 1);
        cp2p.close_session(id1);
        id2 = cp2p.get_peers(id2, nodes2, 61);
        EXPECT_EQ(nodes2.size(), npeers - 1);
        cp2p.close_session(id2);

        for (int k = 0; k < npeers - 1; ++k)
        {
            printf("peer: %s:%u\n", nodes1[k].address().to_string().c_str(), nodes1[k].port());
        }

        for (int j = 0; j < npeers - 1; ++j)
        {
            int k;

            for (k = 0; k < npeers - 1; ++k)
            {
                if (!strcmp(peers[j], (nodes1[k].address().to_string() + ":" + std::to_string(nodes1[k].port())).c_str()))
                    break;
            }
            EXPECT_NE(k, npeers - 1);

            for (k = 0; k < npeers - 1; ++k)
            {
                if (!strcmp(peers[j], (nodes2[k].address().to_string() + ":" + std::to_string(nodes2[k].port())).c_str()))
                    break;
            }
            EXPECT_NE(k, npeers - 1);
        }

        EXPECT_EQ(p2p.save_databases(), true);
    }
}

template <ConsensusType CT>
static void generate_block(
        PostCommittedBlock<CT> &block,
        std::vector<uint8_t> &buf,
        logos::block_hash &hash,
        int sequence,
        int delegate_id)
{
    block.sequence = sequence;
    block.primary_delegate = delegate_id;
    block.previous = hash;
    hash = block.Hash();

    buf.clear();
    block.Serialize(buf, true, true);
    buf.erase(buf.begin(), buf.begin() + 8);
}

template <ConsensusType CT>
ConsensusP2p<CT> *get_cp2p(p2p_interface &p2p, uint32_t &max_saved)
{
    return new ConsensusP2p<CT>(p2p,
        [&max_saved](const PostCommittedBlock<CT> &block, uint8_t id, ValidationStatus *status) -> bool
        {
            bool res;
            if (block.sequence > max_saved + 1)
            {
                if (id >= 20)
                {
                    status->reason = logos::process_result::invalid_tip;
                }
                else if (id >= 10)
                {
                    status->reason = logos::process_result::invalid_request;
                    status->requests[0] = logos::process_result::gap_previous;
                }
                else
                {
                    status->reason = logos::process_result::gap_previous;
                }
                res = false;
            }
            else
            {
                status->reason = logos::process_result::progress;
                res = true;
            }
            printf("Validate(%u,%u) -> %s\n", block.sequence, id, (res ? "true" : "false"));
            return res;
        },
        [&max_saved](const PostCommittedBlock<CT> &block, uint8_t id)
        {
            max_saved = std::max(max_saved, block.sequence);
            printf("ApplyUpdates(%u,%u) -> %d\n", block.sequence, id, max_saved);
         },
        [&max_saved](const PostCommittedBlock<CT> &block) -> bool
        {
            bool res = (block.sequence <= max_saved);
            printf("BlockExists(%u,%u) -> %s\n", block.sequence, block.primary_delegate, (res ? "true" : "false"));
            return res;
        });
}

TEST (P2pTest, VerifyCache)
{
    const char *argv[] = {"unit_test", "-debug=net", 0};
    p2p_config config;

    config.argc = 2;
    config.argv = (char **)argv;
    config.boost_io_service = 0;
    config.test_mode = true;

    config.scheduleAfterMs = [](std::function<void()> const &handler, unsigned ms)
    {
        printf("scheduleAfterMs called.\n");
    };

    config.userInterfaceMessage = [](int type, const char *mess)
    {
        printf("%s%s: %s\n", (type & P2P_UI_INIT ? "init " : ""),
            (type & P2P_UI_ERROR ? "error" : type & P2P_UI_WARNING ? "warning" : "message"), mess);
    };

    p2p_interface p2p;
    EXPECT_EQ(p2p.Init(config), true);

    uint32_t max_savedB = 0, max_savedM = 0, max_savedE = 0;
    ConsensusP2p<ConsensusType::Request>    *cp2pB = get_cp2p<ConsensusType::Request>   (p2p, max_savedB);
    ConsensusP2p<ConsensusType::MicroBlock> *cp2pM = get_cp2p<ConsensusType::MicroBlock>(p2p, max_savedM);
    ConsensusP2p<ConsensusType::Epoch>      *cp2pE = get_cp2p<ConsensusType::Epoch>     (p2p, max_savedE);

    std::function<void (const logos::block_hash &)> _RetryValidateB = cp2pB->_RetryValidate;
    std::function<void (const logos::block_hash &)> _RetryValidateM = cp2pM->_RetryValidate;
    std::function<void (const logos::block_hash &)> _RetryValidateE = cp2pE->_RetryValidate;

    cp2pB->_RetryValidate
        = cp2pM->_RetryValidate
        = cp2pE->_RetryValidate
        = [&_RetryValidateB, &_RetryValidateM, &_RetryValidateE](const logos::block_hash &hash)
            {
                _RetryValidateB(hash);
                _RetryValidateM(hash);
                _RetryValidateE(hash);
            };

    Prequel p;

    PostCommittedBlock<ConsensusType::Request> blockB;
    PostCommittedBlock<ConsensusType::MicroBlock> blockM;
    PostCommittedBlock<ConsensusType::Epoch> blockE;
    std::vector<uint8_t> bufB[5], bufM[5], bufE[5];
    logos::block_hash hashB, hashM, hashE;

    for (uint32_t i = 0; i < 5; ++i)
    {
        generate_block<ConsensusType::Request>  (blockB, bufB[i], hashB, i + 1, 7);
        generate_block<ConsensusType::MicroBlock>       (blockM, bufM[i], hashM, i + 1, 8);
        generate_block<ConsensusType::Epoch>            (blockE, bufE[i], hashE, i + 1, 9);
    }

    /* BatchStateBlocks out of order */
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufB[1].data(), bufB[1].size()), true);
    EXPECT_EQ(max_savedB, 0);
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufB[2].data(), bufB[2].size()), true);
    EXPECT_EQ(max_savedB, 0);
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufB[4].data(), bufB[4].size()), true);
    EXPECT_EQ(max_savedB, 0);
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufB[0].data(), bufB[0].size()), true);
    EXPECT_EQ(max_savedB, 3);
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufB[3].data(), bufB[3].size()), true);
    EXPECT_EQ(max_savedB, 5);

    /* MicroBlocks out of order */
    EXPECT_EQ(cp2pM->ProcessInputMessage(p, bufM[2].data(), bufM[2].size()), true);
    EXPECT_EQ(max_savedM, 0);
    EXPECT_EQ(cp2pM->ProcessInputMessage(p, bufM[1].data(), bufM[1].size()), true);
    EXPECT_EQ(max_savedM, 0);
    EXPECT_EQ(cp2pM->ProcessInputMessage(p, bufM[0].data(), bufM[0].size()), true);
    EXPECT_EQ(max_savedM, 3);
    EXPECT_EQ(cp2pM->ProcessInputMessage(p, bufM[4].data(), bufM[4].size()), true);
    EXPECT_EQ(max_savedM, 3);
    EXPECT_EQ(cp2pM->ProcessInputMessage(p, bufM[3].data(), bufM[3].size()), true);
    EXPECT_EQ(max_savedM, 5);

    /* Epochs out of order */
    EXPECT_EQ(cp2pE->ProcessInputMessage(p, bufE[4].data(), bufE[4].size()), true);
    EXPECT_EQ(max_savedE, 0);
    EXPECT_EQ(cp2pE->ProcessInputMessage(p, bufE[3].data(), bufE[3].size()), true);
    EXPECT_EQ(max_savedE, 0);
    EXPECT_EQ(cp2pE->ProcessInputMessage(p, bufE[0].data(), bufE[0].size()), true);
    EXPECT_EQ(max_savedE, 1);
    EXPECT_EQ(cp2pE->ProcessInputMessage(p, bufE[1].data(), bufE[1].size()), true);
    EXPECT_EQ(max_savedE, 2);
    EXPECT_EQ(cp2pE->ProcessInputMessage(p, bufE[2].data(), bufE[2].size()), true);
    EXPECT_EQ(max_savedE, 5);

    /* BatchStateBlocks with StateBlocks out of order */
    PostCommittedBlock<ConsensusType::Request> blockBS[5];
    logos::block_hash hashS;
    std::vector<uint8_t> bufS[5];
    for (uint32_t i = 0; i < 5; ++i)
    {
        std::shared_ptr<Send> S = make_shared<Send>();
        S->sequence = i;
        S->previous = hashS;
        hashS = S->Hash();
        blockBS[i].requests.push_back(S);
        blockBS[i].hashes.push_back(hashS);
        memset(&hashB, 0, sizeof(hashB));
        generate_block<ConsensusType::Request>(blockBS[i], bufS[i], hashB, i + 1, 10 + i);
    }

    max_savedB = 0;

    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufS[0].data(), bufS[0].size()), true);
    EXPECT_EQ(max_savedB, 1);
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufS[2].data(), bufS[2].size()), true);
    EXPECT_EQ(max_savedB, 1);
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufS[4].data(), bufS[4].size()), true);
    EXPECT_EQ(max_savedB, 1);
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufS[1].data(), bufS[1].size()), true);
    EXPECT_EQ(max_savedB, 3);
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufS[3].data(), bufS[3].size()), true);
    EXPECT_EQ(max_savedB, 5);

    /* Microblock before BatchStateBlock tip */
    std::vector<uint8_t> bufBT, bufMT;

    memset(&hashB, 0, sizeof(hashB));
    generate_block<ConsensusType::Request>(blockB, bufBT, hashB, 1, 0);
    memset(&hashM, 0, sizeof(hashM));
    blockM.tips[0] = hashB;
    generate_block<ConsensusType::MicroBlock>(blockM, bufMT, hashM, 2, 15);

    max_savedB = 0;
    max_savedM = 0;

    EXPECT_EQ(cp2pM->ProcessInputMessage(p, bufMT.data(), bufMT.size()), true);
    EXPECT_EQ(max_savedB, 0);
    EXPECT_EQ(max_savedM, 0);
    max_savedM = 1;
    EXPECT_EQ(cp2pB->ProcessInputMessage(p, bufBT.data(), bufBT.size()), true);
    EXPECT_EQ(max_savedB, 1);
    EXPECT_EQ(max_savedM, 2);

    /* Epoch before MicroBlock tip */
    std::vector<uint8_t> bufET, bufEMT;

    memset(&hashM, 0, sizeof(hashM));
    generate_block<ConsensusType::MicroBlock>(blockM, bufEMT, hashM, 1, 0);
    memset(&hashE, 0, sizeof(hashE));
    blockE.micro_block_tip = hashM;
    generate_block<ConsensusType::Epoch>(blockE, bufET, hashE, 2, 20);

    max_savedM = 0;
    max_savedE = 0;

    EXPECT_EQ(cp2pE->ProcessInputMessage(p, bufET.data(), bufET.size()), true);
    EXPECT_EQ(max_savedM, 0);
    EXPECT_EQ(max_savedE, 0);
    max_savedE = 1;
    EXPECT_EQ(cp2pM->ProcessInputMessage(p, bufEMT.data(), bufEMT.size()), true);
    EXPECT_EQ(max_savedM, 1);
    EXPECT_EQ(max_savedE, 2);


    delete cp2pB;
    delete cp2pM;
    delete cp2pE;
}
