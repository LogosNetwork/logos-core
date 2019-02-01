/// @file
/// This file declares/implements Account Reservations
#pragma once

#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>
#include <logos/common.hpp>
#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/node/delegate_identity_manager.hpp>
//#include <logos/consensus/consensus_container.hpp>

#include <unordered_map>

class ReservationsProvider {
protected:
    using Store = logos::block_store;
public:
    ReservationsProvider(Store & store)
        : _store(store)
    {}
    virtual ~ReservationsProvider() = default;
    virtual bool CanAcquire(const AccountAddress & account, const BlockHash & hash, bool allow_duplicates, MDB_txn * transaction) {return false;}
    virtual bool Acquire(const AccountAddress & account, logos::reservation_info &info) {return true;}
    virtual void Release(const AccountAddress & account, MDB_txn * transaction) {}
    virtual void UpdateReservation(const logos::block_hash & hash, const logos::account account, MDB_txn * transaction) {}
protected:
    Store &      _store;
    Log          _log;
};

class Reservations : public ReservationsProvider
{
protected:

    using ReservationCache = std::unordered_map<AccountAddress, logos::reservation_info>;

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
    bool CanAcquire(const AccountAddress & account, const BlockHash & hash, bool allow_duplicates, MDB_txn * transaction) override
    {
        logos::reservation_info info;
        // Check cache
        if(_reservations.find(account) == _reservations.end())
        {
            // Not in LMDB either
            if (_store.reservation_get(account, info))
            {
                return true;
            }
            else // populate cache with database reservation
            {
                // TODO: Check bootstrap since we might have died and now fallen behind
                _reservations[account] = info;
                return false;
            }
        }
        else
        {
            // We should technically do a sanity check here:
            // if LMDB doesn't contain the reservation then something is seriously wrong
            LOG_WARN(_log) << "Reservations::CanAcquire - Warning - attempt to "
                           << "acquire account "
                           << account.to_string()
                           << " which is already in the Reservations cache.";

            info = _reservations[account];
        }

        // Reservation exists
        if (info.reservation != hash)
        {
            ApprovedEB epoch;
            DelegateIdentityManager::GetCurrentEpoch(_store, epoch);
            uint32_t current_epoch = epoch.epoch_number;
            // This block conflicts with existing reservation.
            if (current_epoch < info.reservation_epoch + PersistenceManager<BSBCT>::RESERVATION_PERIOD)
            {
                return false;
            }
            // remove from cache and DB. If account info check succeeds, it will be reserved later in UpdateReservation.
            else
            {
                Release(account, transaction);
                return true;
            }
        }
        else
        {
            return allow_duplicates;
        }
    }

    void Release(const AccountAddress & account, MDB_txn * transaction) override
    {
        _store.reservation_del(account, transaction);
        LOG_DEBUG(_log) << "Reservations::Release - deleted from blockstore";
        _reservations.erase(account);
        LOG_DEBUG(_log) << "Reservations::Release - erased from cache";
    }

    // Can only be called after checking CanAcquire to ensure we don't corrupt reservation
    // Also need to make sure *not* to call it when another write transaction is waiting in a higher scope, as this will cause hanging
    void UpdateReservation(const logos::block_hash & hash, const logos::account account, MDB_txn * transaction) override
    {
        ApprovedEB epoch;
        DelegateIdentityManager::GetCurrentEpoch(_store, epoch);
        uint32_t current_epoch = epoch.epoch_number;
//        uint32_t current_epoch = ConsensusContainer::GetCurEpochNumber();
        if(_reservations.find(account) != _reservations.end())
        {
            assert (_reservations[account].reservation_epoch + PersistenceManager<BSBCT>::RESERVATION_PERIOD <= current_epoch);
        }
        logos::reservation_info updated_reservation {hash, current_epoch};
        _reservations[account] = updated_reservation;
        _store.reservation_put(account, updated_reservation, transaction);
    }

private:

    ReservationCache _reservations;
};

class DefaultReservations : public ReservationsProvider {
public:
    DefaultReservations(Store & store) : ReservationsProvider(store)
    {}
    ~DefaultReservations() = default;

    bool CanAcquire(const AccountAddress & account, const BlockHash & hash, bool allow_duplicates, MDB_txn * transaction) override
    {
        logos::reservation_info info;
        return !_store.reservation_get(account, info);
    }
    bool Acquire(const AccountAddress & account, logos::reservation_info &info) override
    {
        return _store.reservation_get(account, info);
    }
};