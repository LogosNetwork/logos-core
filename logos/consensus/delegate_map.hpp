#pragma once

#include <memory>
#include <logos/common.hpp>
#include <logos/consensus/consensus_msg_sink.hpp>


class DelegateMap
{

    static std::shared_ptr<DelegateMap> instance;
    using EpochNum = uint32_t;
    using DelegateId = uint8_t;
    template <typename P>
    using WPTR = std::weak_ptr<P>;


    using SinksArr = std::array<std::shared_ptr<ConsensusMsgSink>,NUM_DELEGATES>;

    struct Sinks
    {
        SinksArr arr;
        EpochNum epoch_num = 0;
    };

    Sinks first;
    Sinks second;
    static std::mutex mutex;



    DelegateMap()
    {}

    public:

    static std::shared_ptr<DelegateMap> GetInstance()
    {
        if(!instance)
        {
            instance.reset(new DelegateMap());
        }
        return instance;
    }

    void AddSink(uint32_t epoch, uint8_t remote_id, std::shared_ptr<ConsensusMsgSink> sink)
    {
        //Is this pattern correct? Adding for an old epoch will wrongly put it in new epoch
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch > second.epoch_num && second.epoch_num != 0)
        {
                Log log;
                LOG_INFO(log) << "DelegateMap::AddSink - new epoch, moving";
                //In new epoch, delete backups for epoch_num - 1
                first = std::move(second);
                second.epoch_num = epoch;
        }
        else if(second.epoch_num == 0)
        {
            second.epoch_num = epoch;
        }

        if(second.arr[remote_id])
        {
            Log log;
            LOG_FATAL(log) << "DelegateMap::AddSink - Sink already exists";
            trace_and_halt();
        }
        second.arr[remote_id] = sink;

        Log log;
        LOG_INFO(log) << "DelegateMap::AddSink " << epoch << " - " << unsigned(remote_id);
    }

    std::shared_ptr<ConsensusMsgSink> GetSink(uint32_t epoch, uint8_t remote_id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        Log log;
        LOG_INFO(log) << "DelegateMap::GetSink - " << epoch << " - " << unsigned(remote_id);
        if(epoch == 0)
        {
            Log log;
            LOG_WARN(log) << "DelegateMap::GetSink - epoch is 0. returning nullptr";
            return nullptr;
        }
        if(first.epoch_num == epoch)
        {
            if(!first.arr[remote_id])
            {
                Log log;
                LOG_WARN(log) << "DelegateMap::GetSink - Sink is null";
            }
            return first.arr[remote_id];
        }
        else if(second.epoch_num == epoch)
        {
            if(!second.arr[remote_id])
            {
                Log log;
                LOG_WARN(log) << "DelegateMap::GetSink - Sink is null";
            }
            return second.arr[remote_id];
        }
        else
        {
            Log log;
            LOG_WARN(log) << "DelegateMap::GetSink - No sinks for epoch number";
            return nullptr;
        }
    
    }

    
};
