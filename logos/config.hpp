#pragma once

#include <chrono>
#include <cstddef>

namespace logos
{
// Network variants with different genesis blocks and network parameters
enum class logos_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	logos_test_network,
	// Normal work parameters, secret beta genesis key, beta IP ports
	logos_beta_network,
	// Normal work parameters, secret live key, live IP ports
	logos_live_network
};
logos::logos_networks const logos_network = logos_networks::ACTIVE_NETWORK;
std::chrono::milliseconds const transaction_timeout = std::chrono::milliseconds (1000);
}
