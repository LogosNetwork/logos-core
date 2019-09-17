
template<typename F>
auto
RequestConsensusManager::GenerateSubsets(uint128_t vote,
                                         uint128_t stake,
                                         uint64_t request_count,
                                         const WeightList & weights,
                                         const F & reached_quorum) -> std::list<SupportMap>
{
    // TODO: Hash the set of delegate ID's to make
    //       lookup constant rather than O(n).
    std::list<SupportMap> subsets;

    // For each request, collect the delegate
    // ID's of those delegates that voted for
    // it.
    for(uint64_t i = 0; i < request_count; ++i)
    {
        // The below condition is true if the set of
        // delegates that approve of the request at
        // index i collectively have enough weight to
        // get this request post-committed.
        if(reached_quorum(vote + weights[i].indirect_vote_support,
                          stake + weights[i].indirect_stake_support))
        {
            // Was any other request approved by
            // exactly the same set of delegates?
            auto entry = std::find_if(
                subsets.begin(), subsets.end(),
                [i, &weights](const SupportMap & map)
                {
                    return map.first == weights[i].supporting_delegates;
                });

            // This specific set of supporting delegates
            // doesn't exist yet. Create a new entry.
            if(entry == subsets.end())
            {
                std::unordered_set<uint64_t> indexes;
                indexes.insert(i);

                subsets.push_back(
                    std::make_pair(
                        weights[i].supporting_delegates,
                        indexes));
            }

            // At least one other request was accepted
            // by the same set of delegates.
            else
            {
                entry->second.insert(i);
            }
        }
        else
        {
            // Reject the request at index i.
        }
    }

    // Returns true if all elements
    // in set b can be found in set
    // a.
    auto contains = [](const std::unordered_set<uint8_t> & a,
                       const std::unordered_set<uint8_t> & b)
    {
        for(auto e : b)
        {
            if(a.find(e) == a.end())
            {
                return false;
            }
        }
        return true;
    };

    // Attempt to group requests with overlapping
    // subsets of supporting delegates. This
    // does not find the optimal grouping which
    // would require also considering proper subsets.
    for(auto a = subsets.begin(); a != subsets.end(); ++a)
    {
        auto b = a;
        b++;

        // Compare set A to every set following it
        // in the list.
        for(; b != subsets.end(); ++b)
        {
            auto & a_set = a->first;
            auto & b_set = b->first;

            bool advance = false;

            if(a_set.size() > b_set.size())
            {
                // Does set A contain set B?
                if(contains(a_set, b_set))
                {
                    // Merge sets
                    a->first = b->first;
                    a->second.insert(b->second.begin(),
                                     b->second.end());

                    advance = true;
                }
            }
            else
            {
                // Does set B contain set A?
                if(contains(b_set, a_set))
                {
                    // Merge sets
                    a->second.insert(b->second.begin(),
                                     b->second.end());

                    advance = true;
                }

            }

            // Modifying list while
            // iterating it.
            if(advance)
            {
                auto tmp = b;
                tmp++;
                subsets.erase(b);
                b = tmp;
            }
        }
    }
}
