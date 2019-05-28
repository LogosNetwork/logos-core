// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SYNC_H
#define BITCOIN_SYNC_H

#include <condition_variable>
#include <thread>
#include <mutex>
#include <threadsafety.h>

typedef std::mutex Mutex;
typedef std::recursive_mutex CCriticalSection;

#define PASTE(x, y) x ## y
#define PASTE2(x, y) PASTE(x, y)

#define LOCK(mutex) std::unique_lock<typeof(mutex)> PASTE2(criticalblock, __COUNTER__)(mutex)
#define WAIT_LOCK(mutex, name) std::unique_lock<typeof(mutex)> name(mutex)
#define TRY_LOCK(mutex, name) std::unique_lock<typeof(mutex)> name(mutex, std::try_to_lock)

#define ENTER_CRITICAL_SECTION(mutex) mutex.lock();
#define LEAVE_CRITICAL_SECTION(mutex) mutex.unlock();

#define AssertLockHeld(cs) ASSERT_EXCLUSIVE_LOCK(cs)

class CSemaphore
{
private:
    std::condition_variable condition;
    std::mutex mutex;
    int value;

public:
    explicit CSemaphore(int init)
        : value(init)
    {
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&]()
            {
                return value >= 1;
            });
        value--;
    }

    bool try_wait()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (value < 1)
            return false;
        value--;
        return true;
    }

    void post()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            value++;
        }
        condition.notify_one();
    }
};

/** RAII-style semaphore lock */
class CSemaphoreGrant
{
private:
    CSemaphore* sem;
    bool fHaveGrant;

public:
    void Acquire()
    {
        if (fHaveGrant)
            return;
        sem->wait();
        fHaveGrant = true;
    }

    void Release()
    {
        if (!fHaveGrant)
            return;
        sem->post();
        fHaveGrant = false;
    }

    bool TryAcquire()
    {
        if (!fHaveGrant && sem->try_wait())
            fHaveGrant = true;
        return fHaveGrant;
    }

    void MoveTo(CSemaphoreGrant& grant)
    {
        grant.Release();
        grant.sem = sem;
        grant.fHaveGrant = fHaveGrant;
        fHaveGrant = false;
    }

    CSemaphoreGrant()
        : sem(nullptr)
        , fHaveGrant(false)
    {
    }

    explicit CSemaphoreGrant(CSemaphore& sema,
                             bool fTry = false)
        : sem(&sema)
        , fHaveGrant(false)
    {
        if (fTry)
            TryAcquire();
        else
            Acquire();
    }

    ~CSemaphoreGrant()
    {
        Release();
    }

    operator bool() const
    {
        return fHaveGrant;
    }
};

#endif // BITCOIN_SYNC_H
