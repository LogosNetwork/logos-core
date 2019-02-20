/// @file
/// This file implements base Reservations class

#include <logos/consensus/persistence/reservations.hpp>
#include <logos/consensus/consensus_container.hpp>

// TODO: We should only write to database when the program terminates on an uncaught exception;
//  otherwise we suffer from  a major performance hit
bool
Reservations::CanAcquire(const AccountAddress & account,
                         const BlockHash & hash,
                         bool allow_duplicates)
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
        // Return false if this block conflicts with an existing reservation that hasn't expired.
        // If account info check succeeds, it will be reserved later in UpdateReservation.
        return current_epoch >= info.reservation_epoch + PersistenceManager<R>::RESERVATION_PERIOD;
    }
    else
    {
        return allow_duplicates;
    }
}

void
Reservations::Release(const AccountAddress & account)
{
    _reservations.erase(account);
}

void
Reservations::UpdateReservation(const BlockHash & hash,
                                const AccountAddress & account)
{
    uint32_t current_epoch = ConsensusContainer::GetCurEpochNumber();

    if(_reservations.find(account) != _reservations.end() &&
           _reservations[account].reservation != hash))
    {
        if (_reservations[account].reservation_epoch + PersistenceManager<R>::RESERVATION_PERIOD > current_epoch)
        {
            LOG_FATAL(_log) << "Reservations::UpdateReservation - update called before reservation epoch expiration!";
            trace_and_halt();
        }
    }
    logos::reservation_info updated_reservation {hash, current_epoch};
    _reservations[account] = updated_reservation;
}

bool
DefaultReservations::CanAcquire(const AccountAddress & account,
                                const BlockHash & hash,
                                bool allow_duplicates)
{
    logos::reservation_info info;
    return !_store.reservation_get(account, info);
}
