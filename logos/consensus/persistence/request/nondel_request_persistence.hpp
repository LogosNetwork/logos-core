/// @file
/// This file declares batch state block non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/request/request_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>
#include <logos/consensus/persistence/validator_builder.hpp>
#include <logos/consensus/persistence/nondel_persistence.hpp>

template<>
class NonDelPersistenceManager<R> : public PersistenceManager<R>, public NonDelegatePersistence<R>
{
public:
    using PersistenceManager<R>::Validate;

    NonDelPersistenceManager(Store &store,
                             Milliseconds clock_drift = ZERO_CLOCK_DRIFT)
        : PersistenceManager<R>(store, nullptr, clock_drift)
        , NonDelegatePersistence<R>(store)
    {}

    bool ValidatePreprepare(const PrePrepare & message, ValidationStatus * status)
    {
        using namespace logos;

        ApprovedBSB previous;
        if (message.previous != 0 && _store.batch_block_get(message.previous, previous))
        {
            UpdateStatusReason(status, logos::process_result::gap_previous);
            return false;
        }

        if(_clock_drift > ZERO_CLOCK_DRIFT)
        {
            if (!ValidateTimestamp(message.timestamp))
            {
                UpdateStatusReason(status, logos::process_result::clock_drift);
                return false;
            }
        }

        if (message.previous != 0 && message.sequence != (previous.sequence + 1))
        {
            UpdateStatusReason(status, logos::process_result::wrong_sequence_number);
            return false;
        }

        return PersistenceManager<R>::Validate(message, status);
    }
};
