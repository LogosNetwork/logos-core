#include <gtest/gtest.h>

#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../consensus/consensus_p2p.hpp"

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
    ConsensusP2p<ConsensusType::BatchStateBlock> cp2p(p2p,
        [](const PostCommittedBlock<ConsensusType::BatchStateBlock> &block, uint8_t id, ValidationStatus *status) -> bool
        {
            printf("Validate(%d,%d) called\n", block.sequence, id);
            status->reason = logos::process_result::gap_previous;
            return false;
        },
        [](const PostCommittedBlock<ConsensusType::BatchStateBlock> &block, uint8_t id)
        {
            printf("ApplyUpdates(%d,%d) called\n", block.sequence, id);
        },
        [](const PostCommittedBlock<ConsensusType::BatchStateBlock> &block) -> bool
        {
            printf("BlockExists(%d,%d) called\n", block.sequence, block.primary_delegate);
            return false;
        });

    EXPECT_EQ(p2p.Init(config), true);

    PostCommittedBlock<ConsensusType::BatchStateBlock> block1;
    block1.sequence = 1;
    block1.primary_delegate = 5;

    std::vector<uint8_t> buf0, buf;
    block1.Serialize(buf0, true, true);
    buf.push_back(4), buf.push_back(0), buf.push_back(0), buf.push_back(0);
    buf.push_back(2), buf.push_back(logos_version),
    buf.push_back((int)ConsensusType::BatchStateBlock), buf.push_back(block1.primary_delegate);
    while (buf0.size() & 3) buf0.push_back(0);
    buf.push_back(buf0.size() & 0xff), buf.push_back(buf0.size() / 0x100), buf.push_back(0), buf.push_back(0);
    buf.insert(buf.end(), buf0.begin(), buf0.end());

    EXPECT_EQ(cp2p.ProcessInputMessage(buf.data(), buf.size()), true);
}
