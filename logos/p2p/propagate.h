// Copyright (c) 2018 Logos Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file contains external interface for p2p subsystem based on bitcoin project

#ifndef _PROPAGATE_H_INCLUDED
#define _PROPAGATE_H_INCLUDED

#include <stdint.h>
#include <string.h>
#include <vector>
#include <mutex>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include "hash.h"

#define DEFAULT_PROPAGATE_STORE_SIZE	0x10000

struct PropagateMessage
{
    std::vector<uint8_t>    message;
    uint64_t                label;
    uint256                 hash;
    struct ByHash {};
    struct ByLabel {};

    PropagateMessage(const void *mess,
                     unsigned size)
    {
        message.resize(size);
        memcpy(message.data(), mess, size);
        hash = Hash(message.begin(), message.end());
    }
};

class PropagateStore
{
private:
    uint64_t    max_size;
    uint64_t    first_label;
    uint64_t    next_label;
    boost::multi_index_container<PropagateMessage,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<PropagateMessage::ByHash>,
                boost::multi_index::member<PropagateMessage,uint256,&PropagateMessage::hash>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<PropagateMessage::ByLabel>,
                boost::multi_index::member<PropagateMessage,uint64_t,&PropagateMessage::label>
            >
        >
    >           store;
    std::mutex  mutex;

    bool _Find(const PropagateMessage &mess)
    {
        return store.get<PropagateMessage::ByHash>().find(mess.hash) != store.get<PropagateMessage::ByHash>().end();
    }

public:
    PropagateStore(uint64_t size = DEFAULT_PROPAGATE_STORE_SIZE)
        : max_size(size)
        , first_label(0)
        , next_label(0)
    {}

    ~PropagateStore()
    {}

    bool Find(const PropagateMessage &mess)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return _Find(mess);
    }

    bool Insert(PropagateMessage &mess)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!_Find(mess))
        {
            while (store.size() >= max_size && first_label < next_label)
            {
                auto iter = store.get<PropagateMessage::ByLabel>().find(first_label);
                if (iter != store.get<PropagateMessage::ByLabel>().end())
                {
                    store.get<PropagateMessage::ByLabel>().erase(iter);
                }
                first_label++;
            }
            mess.label = next_label++;
            store.insert(mess);
            return true;
        }
        return false;
    }

    const PropagateMessage *GetNext(uint64_t &current_label)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (current_label < first_label)
        {
            current_label = first_label;
        }

        while (current_label < next_label)
        {
            auto iter = store.get<PropagateMessage::ByLabel>().find(current_label);
            current_label++;
            if (iter != store.get<PropagateMessage::ByLabel>().end())
            {
                const PropagateMessage &mess = *iter;
                return &mess;
            }
        }

        return 0;
    }
};

#endif
