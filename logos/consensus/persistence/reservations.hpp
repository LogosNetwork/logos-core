/// @file
/// This file declares/implements Account Reservations
#pragma once

#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>

#include <unordered_map>

namespace
{

using logos::AccountType;

}

class ReservationsProvider
{
protected:

    using Store           = logos::block_store;
    using AccountPtr      = std::shared_ptr<logos::Account>;
    using TokenAccountPtr = std::shared_ptr<TokenAccount>;
    using LogosAccountPtr = std::shared_ptr<logos::account_info>;

public:

    ReservationsProvider(Store & store)
        : _store(store)
    {}

    virtual ~ReservationsProvider() = default;

    virtual bool Acquire(const AccountAddress & account,
                         AccountPtr & info,
                         AccountType type = AccountType::LogosAccount)
    { return true; }

    virtual bool Acquire(const AccountAddress & account,
                         TokenAccountPtr & info)
    { return true; }

    virtual bool Acquire(const AccountAddress & account,
                         LogosAccountPtr & info)
    { return true; }

    virtual void Release(const AccountAddress & account,
                         AccountType type = AccountType::LogosAccount)
    {}

protected:

    Store & _store;
    Log     _log;
};

class Reservations : public ReservationsProvider
{
protected:

    using AccountCache = std::unordered_map<AccountAddress, AccountPtr>;

public:

    Reservations(Store & store)
        : ReservationsProvider(store)
    {}

    virtual ~Reservations() = default;

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
    bool Acquire(const AccountAddress & account,
                 AccountPtr & info,
                 AccountType type = AccountType::LogosAccount) override
    {
        std::lock_guard<std::mutex> lock(_mutex);

        auto & cache = GetCache(type);

        if(cache.find(account) != cache.end())
        {
            LOG_WARN(_log) << "Reservations::Acquire - Warning - attempt to "
                           << "acquire account "
                           << account.to_string()
                           << " which is already in the Reservations cache.";

            info = cache[account] ;
            return false;
        }

        auto ret = _store.account_get(account, info, type);

        if(!ret)
        {
            cache[account] = info;
        }

        return ret;
    }

    bool Acquire(const AccountAddress & account,
                 TokenAccountPtr & info) override
    {
        auto tmp = static_pointer_cast<logos::Account>(info);
        return Acquire(account, tmp, logos::AccountType::TokenAccount);
    }

    bool Acquire(const AccountAddress & account,
                 LogosAccountPtr & info) override
    {
        auto tmp = static_pointer_cast<logos::Account>(info);
        return Acquire(account, tmp, logos::AccountType::LogosAccount);
    }

    void Release(const AccountAddress & account,
                 AccountType type = AccountType::LogosAccount) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        GetCache(type).erase(account);
    }

private:

    AccountCache & GetCache(AccountType type)
    {
        return type == AccountType::LogosAccount ?
               _accounts : _token_accounts;
    }

    AccountCache _accounts;
    AccountCache _token_accounts;
    std::mutex   _mutex;
};

class DefaultReservations : public ReservationsProvider
{

public:

    DefaultReservations(Store & store)
        : ReservationsProvider(store)
    {}

    virtual ~DefaultReservations() = default;

    bool Acquire(const AccountAddress & account,
                 AccountPtr & info,
                 AccountType type = AccountType::LogosAccount) override
    {
        return _store.account_get(account, info);
    }
};