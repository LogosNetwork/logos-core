/// @file
/// This file declares microblock non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>
#include <logos/consensus/persistence/validator_builder.hpp>

template<>
class NonDelPersistenceManager<MBCT> : public PersistenceManager<MBCT>
{
public:
    using PersistenceManager<MBCT>::Validate;

    NonDelPersistenceManager(Store &store,
                             Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT)
        : PersistenceManager<MBCT>(store, nullptr, clock_drift)
        , _builder(store)
    {}

    bool Validate(const PrePrepare & message, uint8_t remote_delegate_id, ValidationStatus * status)
    {
        using namespace logos;

        if (!_builder.GetValidator(message.epoch_number)->Validate(message, remote_delegate_id))
        {
            LOG_WARN(_log) << "NonDelPersistenceManager::Validate failed to validate microblock signature "
                           << message.epoch_number << " " << (int) remote_delegate_id;
            UpdateStatusReason(status, process_result::bad_signature);
            return false;
        }

        if (!ValidateTimestamp(message.timestamp))
        {
            LOG_WARN(_log) << "NonDelPersistenceManager::Validate failed to validate microblock timestamp "
                           << (int) remote_delegate_id;
            UpdateStatusReason(status, process_result::clock_drift);
            return false;
        }

        return PersistenceManager<MBCT>::Validate(message, remote_delegate_id, status);
    }
private:
    ValidatorBuilder    _builder;
};