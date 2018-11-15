/// @file
/// This file contains declaration of the RecallHandler class, which is used
/// to handle Recall specifics

#pragma once

#include <atomic>

class IRecallHandler
{
public:
    IRecallHandler() = default;
    virtual ~IRecallHandler() = default;
    virtual bool IsRecall() = 0;
    virtual void Reset() = 0;
};

class RecallHandler : public IRecallHandler
{
public:
    RecallHandler()
        : _is_recall(false)
    {}
    ~RecallHandler() = default;

    bool IsRecall() override
    {
        return _is_recall.load();
    }

    void Reset() override
    {
        _is_recall = false;
    }

private:
    std::atomic_bool    _is_recall;

};

