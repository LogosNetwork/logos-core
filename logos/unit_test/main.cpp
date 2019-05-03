#include <logos/node/node.hpp>
#include <gtest/gtest.h>
#include <bls/bls.h>

GTEST_API_ int main(int argc, char **argv) {
  bls::init();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
