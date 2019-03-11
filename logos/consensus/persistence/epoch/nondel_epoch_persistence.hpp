/// @file
/// This file declares epoch non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>
#include <logos/consensus/persistence/validator_builder.hpp>
#include <logos/consensus/persistence/nondel_persistence.hpp>

template<>
class NonDelPersistenceManager<ECT> : public PersistenceManager<ECT>, public NonDelegatePersistence<ECT>
{
public:
    using PersistenceManager<ECT>::Validate;

    NonDelPersistenceManager(Store &store,
                             Milliseconds clock_drift = ZERO_CLOCK_DRIFT)
        : PersistenceManager<ECT>(store, nullptr, clock_drift)
        , NonDelegatePersistence<ECT>(store)
    {}

    bool ValidatePreprepare(const PrePrepare & pre_prepare, ValidationStatus * status)
    {
        if(_clock_drift > Milliseconds(0))
        {
            if (!ValidateTimestamp(pre_prepare.timestamp))
            {
                LOG_WARN(_logger) << "NonDelPersistenceManager::Validate failed to validate microblock timestamp";
                UpdateStatusReason(status, logos::process_result::clock_drift);
                return false;
            }
        }
        return PersistenceManager<ECT>::Validate(pre_prepare, status);
    }
};
