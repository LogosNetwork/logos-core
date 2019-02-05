/// @file
/// This file implements base Reservations class

#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/consensus_container.hpp>

// TODO: We should only write to database when the program terminates on an uncaught exception;
//  otherwise we suffer from  a major performance hit
bool
Reservations::CanAcquire(const AccountAddress & account, const BlockHash & hash, bool allow_duplicates, MDB_txn * transaction)
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
        uint32_t current_epoch = ConsensusContainer::GetCurEpochNumber();
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

void
Reservations::Release(const AccountAddress & account, MDB_txn * transaction)
{
    _store.reservation_del(account, transaction);
    _reservations.erase(account);
}

void
Reservations::UpdateReservation(const logos::block_hash & hash, const logos::account & account, MDB_txn * transaction)
{
    uint32_t current_epoch = ConsensusContainer::GetCurEpochNumber();
    if(_reservations.find(account) != _reservations.end())
    {
        if (_reservations[account].reservation_epoch + PersistenceManager<BSBCT>::RESERVATION_PERIOD > current_epoch)
        {
            LOG_FATAL(_log) << "Reservations::UpdateReservation - update called before reservation epoch expiration!";
            trace_and_halt();
        }
    }
    logos::reservation_info updated_reservation {hash, current_epoch};
    _reservations[account] = updated_reservation;
    _store.reservation_put(account, updated_reservation, transaction);
}

bool
DefaultReservations::CanAcquire(const AccountAddress & account, const BlockHash & hash, bool allow_duplicates, MDB_txn * transaction)
{
    logos::reservation_info info;
    return !_store.reservation_get(account, info);
}
