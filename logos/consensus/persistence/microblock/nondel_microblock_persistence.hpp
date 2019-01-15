/// @file
/// This file declares microblock non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>
#include <logos/consensus/persistence/validator_builder.hpp>
#include <logos/consensus/persistence/nondel_persistence.hpp>

template<>
class NonDelPersistenceManager<MBCT> : public PersistenceManager<MBCT>, public NoneDelegatePersistence<MBCT>
{
public:
    using PersistenceManager<MBCT>::Validate;

    NonDelPersistenceManager(Store &store,
                             Milliseconds clock_drift = 0)
        : PersistenceManager<MBCT>(store, nullptr, clock_drift)
        , NoneDelegatePersistence<MBCT>(store)
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
        return PersistenceManager<MBCT>::Validate(pre_prepare, status);
    }
};
