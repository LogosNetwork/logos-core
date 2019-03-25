#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace logos
{
// Lower priority of calling work generating thread
void work_thread_reprioritize ();
template <typename... T>
class observer_set
{
public:
    void add (std::function<void(T...)> const & observer_a)
    {
        std::lock_guard<std::mutex> lock (mutex);
        observers.push_back (observer_a);
    }
    void operator() (T... args)
    {
        std::lock_guard<std::mutex> lock (mutex);
        for (auto & i : observers)
        {
            i (args...);
        }
    }
    std::mutex mutex;
    std::vector<std::function<void(T...)>> observers;
};

std::string string_to_hex_str(const std::string& input);

std::string byte_vector_to_string(const std::vector<uint8_t> & buf);

}

struct SelfBase : std::enable_shared_from_this<SelfBase> {
    virtual ~SelfBase() = default;
};

template <typename T>
struct Self : virtual SelfBase {
    virtual ~Self() = default;
    std::shared_ptr<T> shared_from_this() {
        return std::dynamic_pointer_cast<T>(SelfBase::shared_from_this());
    }
};
