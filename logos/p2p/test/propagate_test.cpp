#include <gtest/gtest.h>

#include <vector>
#include <cstdint>
#include "../propagate.h"

#define MAX_CAPACITY    10u
#define MAX_SIZE        0x1000

static std::vector<uint8_t> random_vector(size_t maxsize)
{
    std::vector<uint8_t> v;
    unsigned size = rand() % maxsize;
    for (unsigned i = 0; i < size; ++i)
        v.push_back(rand() % 0x100);
    printf("random vector of size %03x: ", v.size());
    for (unsigned i = 0; i < std::min(v.size(), (size_t)16); ++i)
        printf(" %02x", v[i]);
    printf("\n");
    return v;
}

TEST (PropagateTest, VerifyCapacity)
{
    PropagateStore s(MAX_CAPACITY);
    std::vector<std::vector<uint8_t>> vv;

    for (unsigned i = 0; i < MAX_CAPACITY * 2; ++i)
        vv.push_back(random_vector(MAX_SIZE));

    for (unsigned i = 0; i < vv.size(); ++i)
    {
        for (unsigned j = 0; j < vv.size(); ++j)
        {
            PropagateMessage m(vv[j].data(), vv[j].size());
            bool res = s.Find(m);
            if (j >= i || i - j > MAX_CAPACITY)
                EXPECT_EQ (res, false);
            else
                EXPECT_EQ (res, true);
        }

        PropagateMessage m(vv[i].data(), vv[i].size());
        EXPECT_EQ (s.Insert(m), true);

        unsigned n = i - rand() % std::min(i + 1, MAX_CAPACITY);
        PropagateMessage m0(vv[n].data(), vv[n].size());
        EXPECT_EQ (s.Insert(m0), false);
    }
}
