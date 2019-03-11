/// @file
/// This file declares microblock non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>
#include <logos/consensus/persistence/validator_builder.hpp>
#include <logos/consensus/persistence/nondel_persistence.hpp>

template<>
class NonDelPersistenceManager<MBCT> : public PersistenceManager<MBCT>, public NonDelegatePersistence<MBCT>
{
public:
    using PersistenceManager<MBCT>::Validate;

    NonDelPersistenceManager(Store &store,
                             Milliseconds clock_drift = ZERO_CLOCK_DRIFT)
        : PersistenceManager<MBCT>(store, nullptr, clock_drift)
        , NonDelegatePersistence<MBCT>(store)
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
        return PersistenceManager<MBCT>::Validate(pre_prepare, status);
    }
};
