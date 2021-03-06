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
#include <hash.h>

#define DEFAULT_PROPAGATE_STORE_SIZE        0x10000
#define DEFAULT_PROPAGATE_IMPORTANT_SIZE    0x1000
#define DEFAULT_PROPAGATE_HASH_SIZE         0x100000
#define PROPAGATE_HASH_BUCKET_LOG           4
#define PROPAGATE_HASH_BUCKET_SIZE          (1 << PROPAGATE_HASH_BUCKET_LOG)

/*
 * This hash table by default has 2^16 buckets, each bucket has 16 entries.
 * In total there is 2^20 entries.
 * Entries in each bucket are shifted when new entry come.
 * Statistically it looks like FIFO.
 */
class PropagateHash
{
    using cheap_hash = uint64_t;

public:
    PropagateHash(size_t size)
        : _buckets_mask(size / PROPAGATE_HASH_BUCKET_SIZE - 1)
        , _data(new cheap_hash[size]())
    {
    }

    ~PropagateHash()
    {
        delete[] _data;
    }

    bool find(const uint256 hash) const
    {
        cheap_hash chash = hash.GetCheapHash() | 1, chash1 = hash.GetCheapHash(sizeof(cheap_hash));
        size_t index = chash1 & _buckets_mask;
        const cheap_hash *bucket = &_data[index << PROPAGATE_HASH_BUCKET_LOG];
        for (int i = 0; i < PROPAGATE_HASH_BUCKET_SIZE; ++i)
        {
            if (bucket[i] == chash)
            {
                return true;
            }
        }
        return false;
    }

    void insert(const uint256 hash)
    {
        cheap_hash chash = hash.GetCheapHash() | 1, tmp = chash, chash1 = hash.GetCheapHash(sizeof(cheap_hash));
        size_t index = chash1 & _buckets_mask;
        cheap_hash *bucket = &_data[index << PROPAGATE_HASH_BUCKET_LOG];
        for (int i = 0; i < PROPAGATE_HASH_BUCKET_SIZE; ++i)
        {
            std::swap(tmp, bucket[i]);
            if (tmp == chash)
            {
                break;
            }
        }
    }

private:
    size_t          _buckets_mask;
    cheap_hash *    _data;
};

typedef std::shared_ptr<std::vector<uint8_t>> PropagateMessageHandle;

struct PropagateMessage
{
    PropagateMessageHandle  message;
    uint64_t                label;
    uint256                 hash;
    bool                    important;
    struct ByHash {};
    struct ByLabel {};

    PropagateMessage(const void *mess,
                     unsigned size,
                     bool is_important = false)
    {
        message = std::make_shared<std::vector<uint8_t>>();
        message->resize(size);
        memcpy(message->data(), mess, size);
        hash = Hash(message->begin(), message->end());
        important = is_important;
    }
};

class PropagateFIFO
{
private:
    uint64_t        max_size;
    uint64_t        first_label;
    uint64_t        next_label;
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
    >               store;

    bool _Find(const PropagateMessage &mess)
    {
        return store.get<PropagateMessage::ByHash>().find(mess.hash) != store.get<PropagateMessage::ByHash>().end();
    }

public:
    PropagateFIFO(uint64_t size = DEFAULT_PROPAGATE_STORE_SIZE)
        : max_size(size)
        , first_label(0)
        , next_label(0)
    {
    }

    ~PropagateFIFO()
    {
    }

    bool Find(const PropagateMessage &mess)
    {
        return _Find(mess);
    }

    bool Insert(PropagateMessage &mess)
    {
        if (!_Find(mess))
        {
            while (store.size() >= max_size && first_label < next_label)
            {
                auto iter = store.get<PropagateMessage::ByLabel>().find(first_label);
                if (iter != store.get<PropagateMessage::ByLabel>().end())
                    store.get<PropagateMessage::ByLabel>().erase(iter);
                first_label++;
            }
            mess.label = next_label++;
            store.insert(mess);
            return true;
        }
        return false;
    }

    uint64_t GetNextLabel() const
    {
        return next_label;
    }

    const PropagateMessage *GetNext(uint64_t &current_label)
    {
        if (current_label < first_label)
            current_label = first_label;

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

class PropagateStore
{
private:
    PropagateHash   hash;
    PropagateFIFO   regular;
    PropagateFIFO   important;
    std::mutex      mutex;

    bool _Find(const PropagateMessage &mess)
    {
        return hash.find(mess.hash) || regular.Find(mess) || important.Find(mess);
    }

public:
    PropagateStore(uint64_t size = DEFAULT_PROPAGATE_STORE_SIZE,
                   uint64_t hash_size = DEFAULT_PROPAGATE_HASH_SIZE,
                   uint64_t important_size = DEFAULT_PROPAGATE_IMPORTANT_SIZE)
        : hash(hash_size)
        , regular(size)
        , important(important_size)
    {
    }

    ~PropagateStore()
    {
    }

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
            if (mess.important)
                important.Insert(mess);
            else
                regular.Insert(mess);
            hash.insert(mess.hash);
            return true;
        }
        return false;
    }

    uint64_t GetNextLabel() const
    {
        return regular.GetNextLabel();
    }

    /* This function is used only in unit tests, use GetNextHandle() elsewhere. */
    const PropagateMessage *GetNext(uint64_t &current_label)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return regular.GetNext(current_label);
    }

    PropagateMessageHandle GetNextHandle(uint64_t &current_label, uint64_t &important_label)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const PropagateMessage *res = important.GetNext(important_label);
        if (!res) res = regular.GetNext(current_label);
        if (res) return res->message;
        return PropagateMessageHandle();
    }
};

#endif
