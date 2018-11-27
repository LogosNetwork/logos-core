/// @file
/// This file declares microblock non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/microblock/microblock_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>

template<>
class NonDelPersistenceManager<MBCT> : public PersistenceManager<MBCT>
{
    NonDelPersistenceManager(MessageValidator &validator,
                             Store &store,
                             Milliseconds clock_drift)
        : PersistenceManager<MBCT>(validator, store, nullptr, clock_drift)
    {}

    bool Validate(conse PrePrepare & message, uint8_t remote_delegate_id, ValidationStatus * status)
    {
        using namespace logos;

        if (!_validator.Validate(message, remote_delegate_id))
        {
            UpdateStatusReason(status, process_result::bad_signature);
            return false;
        }

        if (!ValidateTimestamp(message.timestamp))
        {
            UpdateStatusReason(status, process_result::clock_drift);
            return false;
        }

        return PersistenceManager<MBCT>::Validate(message, remote_delegate_id, status);
    }

    bool Validate(const PostCommit & message, uint8_t remote_delegate_id)
    {
        return _validator.Validate(message, remote_delegate_id);
    }

    void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id)
    {
        PersistenceManager<MBCT>::ApplyUpdates(message, delegate_id);
    }
};