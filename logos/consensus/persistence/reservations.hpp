/// @file
/// This file declares/implements Account Reservations
#pragma once

#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <unordered_map>

class ReservationsProvider {
protected:
    using Store = logos::block_store;
public:
    ReservationsProvider(Store & store)
        : _store(store)
    {}
    virtual ~ReservationsProvider() = default;
    virtual bool Acquire(const logos::account & account, logos::account_info &info) {return true;}
    virtual void Release(const logos::account & account) {}
    virtual bool UpdateReservation(const logos::block_hash & hash, const uint64_t current_epoch, const logos::account account) {return false;}
protected:
    Store &      _store;
    Log          _log;
};

class Reservations : public ReservationsProvider
{
protected:

    using AccountCache = std::unordered_map<logos::account, logos::account_info>;

public:
    Reservations(Store & store)
        : ReservationsProvider(store)
    {}
    ~Reservations() = default;

    //-------------------------------------------------------------------------
    // XXX - It is possible for a delegate D1 that has validated/Post-Comitted
    //       (but hasn't yet updated its database and cleared the reservation)
    //       a send request from account A1 to receive the subsequent request
    //       from account A1 as a backup delegate for a PrePrepare from another
    //       delegate D2. In this case D1 would reject a valid send transaction
    //       from A1 since A1 would appear to still be reserved. However, this
    //       is unlikely, as for this to occur, the Post-Commit would have to
    //       propagate to both D2 and to the client before D1 clears the
    //       reservation. When this occurs, D1 will attempt to Acquire an
    //       account that is already stored in the Reservations cache. However,
    //       this is not the only case in which a cached account will be
    //       acquired.
    //-------------------------------------------------------------------------
    bool Acquire(const logos::account & account, logos::account_info &info) override
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_accounts.find(account) != _accounts.end())
        {
            LOG_WARN(_log) << "Reservations::Acquire - Warning - attempt to "
                           << "acquire account "
                           << account.to_string()
                           << " which is already in the Reservations cache.";

            info = _accounts[account];
            return false;
        }

        auto ret = _store.account_get(account, info);

        if(!ret)
        {
            _accounts[account] = info;
        }

        return ret;
    }

    void Release(const logos::account & account) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _accounts.erase(account);
    }

    bool UpdateReservation(const logos::block_hash & hash, const uint64_t current_epoch, const logos::account account) override
    {
        std::lock_guard<std::mutex> lock(_mutex);  // SYL Integration fix - resource protection
        if(_accounts.find(account) == _accounts.end())
        {
            LOG_ERROR(_log) << "Reservations::UpdateReservation - unable to find account to update. ";
            return true;
        }
        _accounts[account].reservation = hash;
        _accounts[account].reservation_epoch = current_epoch;
        return false;
    }

private:

    AccountCache _accounts;
    std::mutex   _mutex;
};

class DefaultReservations : public ReservationsProvider {
public:
    DefaultReservations(Store & store) : ReservationsProvider(store)
    {}
    ~DefaultReservations() = default;

    bool Acquire(const logos::account & account, logos::account_info &info) override
    {
        return _store.account_get(account, info);
    }
};