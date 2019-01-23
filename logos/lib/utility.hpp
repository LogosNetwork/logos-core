#pragma once

#include <logos/lib/blocks.hpp>

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

template<size_t N>
struct BitField
{
    using GetField = std::function<std::string (size_t i)>;
    using GetPos   = std::function<size_t (bool &, const std::string &)>;

    BitField(bool & error,
             std::basic_streambuf<uint8_t> & stream)
    {
        error = logos::read(stream, field);
    }

    BitField() = default;

    void Set(size_t pos)
    {
        field.set(pos);
    }

    bool operator[] (size_t pos) const
    {
        return field[pos];
    }

    void DeserializeJson(bool & error,
                         boost::property_tree::ptree const & tree,
                         GetPos f)
    {
        for(const auto & entry : tree)
        {
            size_t pos(f(error, entry.second.get<std::string>("")));
            if(error)
            {
                return;
            }

            field.set(pos);
        }
    }

    boost::property_tree::ptree SerializeJson(GetField f) const
    {
        boost::property_tree::ptree tree;

        for(size_t i = 0; i < N; ++i)
        {
            if(field[i])
            {
                boost::property_tree::ptree t;
                t.put("", f(i));

                tree.push_back(std::make_pair("", t));

                // TODO: t.push_back("",
                //                   boost::property_tree::ptree(f(i))); ?
            }
        }

        return tree;
    }

    void Hash(blake2b_state & hash) const
    {
        using H = std::hash<std::bitset<N>>;

        size_t h(H()(field));
        blake2b_update(&hash, &h, sizeof(h));
    }

    static constexpr size_t WireSize()
    {
        using B = std::bitset<N>;

        return sizeof(typename std::result_of<decltype(&B::to_ullong)(B)>::type);
    }

    std::bitset<N> field;
};
