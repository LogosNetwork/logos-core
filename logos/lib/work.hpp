#pragma once

#include <boost/optional.hpp>
#include <logos/config.hpp>
#include <logos/lib/numbers.hpp>
#include <logos/lib/utility.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>

namespace logos
{
class block;
bool work_validate (logos::block_hash const &, uint64_t);
bool work_validate (logos::block const &);
uint64_t work_value (logos::block_hash const &, uint64_t);
class opencl_work;
class work_pool
{
public:
	work_pool (unsigned, std::function<boost::optional<uint64_t> (logos::uint256_union const &)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (logos::uint256_union const &);
	void generate (logos::uint256_union const &, std::function<void(boost::optional<uint64_t> const &)>);
	uint64_t generate (logos::uint256_union const &);
	std::atomic<int> ticket;
	bool done;
	std::vector<std::thread> threads;
	std::list<std::pair<logos::uint256_union, std::function<void(boost::optional<uint64_t> const &)>>> pending;
	std::mutex mutex;
	std::condition_variable producer_condition;
	std::function<boost::optional<uint64_t> (logos::uint256_union const &)> opencl;
	logos::observer_set<bool> work_observers;
	// Local work threshold for rate-limiting publishing blocks. ~5 seconds of work.
	static uint64_t const publish_test_threshold = 0xff00000000000000;
	static uint64_t const publish_full_threshold = 0xffffffc000000000;
	static uint64_t const publish_threshold = logos::logos_network == logos::logos_networks::logos_test_network ? publish_test_threshold : publish_full_threshold;
};
}
