#pragma once
#include <logos/blockstore.hpp>

std::vector<std::pair<AccountAddress,uint64_t>> getElectionWinners(size_t num_winners, logos::block_store& store);
