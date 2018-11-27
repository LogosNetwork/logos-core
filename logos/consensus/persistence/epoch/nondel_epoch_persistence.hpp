/// @file
/// This file declares epoch non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>

template<>
class NonDelPersistenceManager<ECT> : public PersistenceManager<ECT>
{
    NonDelPersistenceManager(MessageValidator &validator,
                             Store &store,
                             Milliseconds clock_drift)
        : PersistenceManager<ECT>(validator, store, nullptr, clock_drift)
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

        return PersistenceManager<ECT>::Validate(message, remote_delegate_id, status);
    }

    bool Validate(const PostCommit & message, uint8_t remote_delegate_id)
    {
        return _validator.Validate(message, remote_delegate_id);
    }

    void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id)
    {
        PersistenceManager<ECT>::ApplyUpdates(message, delegate_id);
    }
};