#include <logos/lib/work.hpp>

#include <logos/lib/blocks.hpp>
#include <logos/node/xorshift.hpp>

#include <future>

bool logos::work_validate (logos::block_hash const & root_a, uint64_t work_a)
{
	return logos::work_value (root_a, work_a) < logos::work_pool::publish_threshold;
}

bool logos::work_validate (logos::block const & block_a)
{
	return work_validate (block_a.root (), block_a.block_work ());
}

uint64_t logos::work_value (logos::block_hash const & root_a, uint64_t work_a)
{
	uint64_t result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result));
	blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work_a), sizeof (work_a));
	blake2b_update (&hash, root_a.bytes.data (), root_a.bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&result), sizeof (result));
	return result;
}

logos::work_pool::work_pool (unsigned max_threads_a, std::function<boost::optional<uint64_t> (logos::uint256_union const &)> opencl_a) :
ticket (0),
done (false),
opencl (opencl_a)
{
	static_assert (ATOMIC_INT_LOCK_FREE == 2, "Atomic int needed");
	auto count (logos::logos_network == logos::logos_networks::logos_test_network ? 1 : std::min (max_threads_a, std::max (1u, std::thread::hardware_concurrency ())));
	for (auto i (0); i < count; ++i)
	{
		auto thread (std::thread ([this, i]() {
			logos::work_thread_reprioritize ();
			loop (i);
		}));
		threads.push_back (std::move (thread));
	}
}

logos::work_pool::~work_pool ()
{
	stop ();
	for (auto & i : threads)
	{
		i.join ();
	}
}

void logos::work_pool::loop (uint64_t thread)
{
	// Quick RNG for work attempts.
	xorshift1024star rng;
	logos::random_pool.GenerateBlock (reinterpret_cast<uint8_t *> (rng.s.data ()), rng.s.size () * sizeof (decltype (rng.s)::value_type));
	uint64_t work;
	uint64_t output;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (output));
	std::unique_lock<std::mutex> lock (mutex);
	while (!done || !pending.empty ())
	{
		auto empty (pending.empty ());
		if (thread == 0)
		{
			// Only work thread 0 notifies work observers
			work_observers (!empty);
		}
		if (!empty)
		{
			auto current_l (pending.front ());
			int ticket_l (ticket);
			lock.unlock ();
			output = 0;
			// ticket != ticket_l indicates a different thread found a solution and we should stop
			while (ticket == ticket_l && output < logos::work_pool::publish_threshold)
			{
				// Don't query main memory every iteration in order to reduce memory bus traffic
				// All operations here operate on stack memory
				// Count iterations down to zero since comparing to zero is easier than comparing to another number
				unsigned iteration (256);
				while (iteration && output < logos::work_pool::publish_threshold)
				{
					work = rng.next ();
					blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work), sizeof (work));
					blake2b_update (&hash, current_l.first.bytes.data (), current_l.first.bytes.size ());
					blake2b_final (&hash, reinterpret_cast<uint8_t *> (&output), sizeof (output));
					blake2b_init (&hash, sizeof (output));
					iteration -= 1;
				}
			}
			lock.lock ();
			if (ticket == ticket_l)
			{
				// If the ticket matches what we started with, we're the ones that found the solution
				assert (output >= logos::work_pool::publish_threshold);
				assert (work_value (current_l.first, work) == output);
				// Signal other threads to stop their work next time they check ticket
				++ticket;
				pending.pop_front ();
				lock.unlock ();
				current_l.second (work);
				lock.lock ();
			}
			else
			{
				// A different thread found a solution
			}
		}
		else
		{
			// Wait for a work request
			producer_condition.wait (lock);
		}
	}
}

void logos::work_pool::cancel (logos::uint256_union const & root_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	if (!pending.empty ())
	{
		if (pending.front ().first == root_a)
		{
			++ticket;
		}
	}
	pending.remove_if ([&root_a](decltype (pending)::value_type const & item_a) {
		bool result;
		if (item_a.first == root_a)
		{
			item_a.second (boost::none);
			result = true;
		}
		else
		{
			result = false;
		}
		return result;
	});
}

void logos::work_pool::stop ()
{
	std::lock_guard<std::mutex> lock (mutex);
	done = true;
	producer_condition.notify_all ();
}

void logos::work_pool::generate (logos::uint256_union const & root_a, std::function<void(boost::optional<uint64_t> const &)> callback_a)
{
	assert (!root_a.is_zero ());
	boost::optional<uint64_t> result;
	if (opencl)
	{
		result = opencl (root_a);
	}
	if (!result)
	{
		std::lock_guard<std::mutex> lock (mutex);
		pending.push_back (std::make_pair (root_a, callback_a));
		producer_condition.notify_all ();
	}
	else
	{
		callback_a (result);
	}
}

uint64_t logos::work_pool::generate (logos::uint256_union const & hash_a)
{
	std::promise<boost::optional<uint64_t>> work;
	generate (hash_a, [&work](boost::optional<uint64_t> work_a) {
		work.set_value (work_a);
	});
	auto result (work.get_future ().get ());
	return result.value ();
}
