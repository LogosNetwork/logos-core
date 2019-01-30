/// @file
/// This file declares/implements Account Reservations
#pragma once

#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>
#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
//#include <logos/node/delegate_identity_manager.hpp>
//#include <logos/consensus/consensus_container.hpp>

#include <unordered_map>

class ReservationsProvider
{
protected:

    using Store = logos::block_store;

public:
    explicit ReservationsProvider(Store & store)
        : _store(store)
    {}

    virtual ~ReservationsProvider() = default;
    virtual bool CanAcquire(const AccountAddress & account, const BlockHash & hash, bool allow_duplicates) {return false;}
    virtual void Release(const AccountAddress & account) {}
    virtual void UpdateReservation(const logos::block_hash & hash, const logos::account & account) {}

    virtual bool Acquire(const AccountAddress & account,
                         logos::account_info &info)
    { return true; }

protected:

    Store & _store;
    Log     _log;
};

class Reservations : public ReservationsProvider
{
protected:

    using ReservationCache = std::unordered_map<AccountAddress, logos::reservation_info>;

public:
    explicit Reservations(Store & store)
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
    bool CanAcquire(const AccountAddress & account, const BlockHash & hash, bool allow_duplicates) override;

    void Release(const AccountAddress & account) override;

    // Can only be called after checking CanAcquire to ensure we don't corrupt reservation
    void UpdateReservation(const logos::block_hash & hash, const logos::account & account) override;

private:

    ReservationCache _reservations;
};

class DefaultReservations : public ReservationsProvider
{

public:
    explicit DefaultReservations(Store & store)
        : ReservationsProvider(store)
    {}

    virtual ~DefaultReservations() = default;

    bool CanAcquire(const AccountAddress & account, const BlockHash & hash, bool allow_duplicates) override;

    bool Acquire(const AccountAddress & account, logos::account_info & info) override
    {
        return _store.account_get(account, info);
    }
};