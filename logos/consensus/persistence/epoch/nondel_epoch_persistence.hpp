/// @file
/// This file declares epoch non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>
#include <logos/consensus/persistence/validator_builder.hpp>
#include <logos/consensus/persistence/nondel_persistence.hpp>

template<>
class NonDelPersistenceManager<ECT> : public PersistenceManager<ECT>, public NoneDelegatePersistence<ECT>
{
public:
    using PersistenceManager<ECT>::Validate;

    NonDelPersistenceManager(Store &store,
                             Milliseconds clock_drift = 0)
        : PersistenceManager<ECT>(store, nullptr, clock_drift)
        ,  NoneDelegatePersistence<ECT>(store)
    {}

    bool ValidatePreprepare(const PrePrepare & pre_prepare, ValidationStatus * status)
    {
        if(_clock_drift > 0)
        {
            if (!ValidateTimestamp(pre_prepare.timestamp))
            {
                LOG_WARN(_log) << "NonDelPersistenceManager::Validate failed to validate microblock timestamp";
                UpdateStatusReason(status, process_result::clock_drift);
                return false;
            }
        }
        return PersistenceManager<ECT>::Validate(pre_prepare, status);
    }
};
