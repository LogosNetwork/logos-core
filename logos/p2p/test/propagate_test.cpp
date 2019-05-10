#include <gtest/gtest.h>

#include <vector>
#include <string>
#include <cstdint>
#include "../propagate.h"

#define MAX_CAPACITY    10u
#define MAX_SIZE        0x1000
#define HASH_LEN        32

static std::string prepare_digest(const char *str, uint8_t arr[HASH_LEN])
{
    std::string res;
    char buf[3];
    int i;
    for (i = 0; i < HASH_LEN; ++i)
    {
        memcpy(buf, str + 2 * (HASH_LEN - i - 1), 2);
        buf[2] = 0;
        res += buf;
        sscanf(buf, "%hhx", &arr[HASH_LEN - i - 1]);
    }
    return res;
}

TEST (PropagateTest, VerifyHash)
{
    const char *t1 = "";
    uint8_t h1[HASH_LEN];
    std::string s1 = prepare_digest("0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8", h1);
    PropagateMessage m1(t1, strlen(t1));
    const std::string str1 = m1.hash.ToString();
    printf("Hash(\"%s\") = \"%s\"\n", t1, str1.c_str());
    EXPECT_EQ(sizeof(m1.hash), HASH_LEN);
    EXPECT_EQ(m1.hash.size(), HASH_LEN);
    EXPECT_EQ(str1, s1);
    EXPECT_EQ(memcmp(h1, m1.hash.begin(), HASH_LEN), 0);

    const char *t2 = "The quick brown fox jumps over the lazy dog";
    uint8_t h2[HASH_LEN];
    std::string s2 = prepare_digest("01718cec35cd3d796dd00020e0bfecb473ad23457d063b75eff29c0ffa2e58a9", h2);
    PropagateMessage m2(t2, strlen(t2));
    const std::string str2 = m2.hash.ToString();
    printf("Hash(\"%s\") = \"%s\"\n", t2, str2.c_str());
    EXPECT_EQ(str2, s2);
    EXPECT_EQ(memcmp(h2, m2.hash.begin(), HASH_LEN), 0);
}

static std::vector<uint8_t> random_vector(unsigned maxsize)
{
    std::vector<uint8_t> v;
    unsigned size = rand() % maxsize;
    for (unsigned i = 0; i < size; ++i)
        v.push_back(rand() % 0x100);
    printf("random vector of size %03lx: ", v.size());
    for (unsigned i = 0; i < std::min(v.size(), (size_t)16); ++i)
        printf(" %02x", v[i]);
    printf("\n");
    return v;
}

TEST (PropagateTest, VerifyStore)
{
    PropagateStore s(MAX_CAPACITY);
    std::vector<std::vector<uint8_t>> vv;

    for (unsigned i = 0; i < MAX_CAPACITY * 2; ++i)
        vv.push_back(random_vector(MAX_SIZE));

    for (unsigned i = 0; i < vv.size(); ++i)
    {
        uint64_t label = 0;

        for (unsigned j = 0; j < vv.size(); ++j)
        {
            PropagateMessage m(vv[j].data(), vv[j].size());
            bool res = s.Find(m);

            if (j >= i || i - j > MAX_CAPACITY)
                EXPECT_EQ (res, false);
            else
                EXPECT_EQ (res, true);

            uint64_t prev_label = label;
            const PropagateMessage *cm = s.GetNext(label);

            if (j < i && j < MAX_CAPACITY)
            {
                EXPECT_NE((long)cm, 0);
                EXPECT_EQ(label, cm->label + 1);
                EXPECT_LE(prev_label, label);

                PropagateMessage mm(vv[cm->label].data(), vv[cm->label].size());
                EXPECT_EQ(cm->message, mm.message);
                EXPECT_EQ(cm->hash, mm.hash);
            }
            else
            {
                EXPECT_EQ((long)cm, 0);
                EXPECT_EQ(prev_label, label);
            }
        }

        std::vector<uint8_t> &v = vv[i];
        PropagateMessage m(v.data(), v.size());
        EXPECT_EQ (m.message, v);
        EXPECT_EQ (m.hash, Hash(v.begin(), v.end()));
        EXPECT_EQ (s.Insert(m), true);

        unsigned n = i - rand() % std::min(i + 1, MAX_CAPACITY);
        PropagateMessage m0(vv[n].data(), vv[n].size());
        EXPECT_EQ (s.Insert(m0), false);
    }
}
