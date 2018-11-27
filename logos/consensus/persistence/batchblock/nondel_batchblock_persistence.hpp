/// @file
/// This file declares batch state block non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>

template<>
class NonDelPersistenceManager<BSBCT> : public PersistenceManager<BSBCT>
{
    NonDelPersistenceManager(MessageValidator &validator,
                             Store &store,
                             Milliseconds clock_drift)
        : PersistenceManager<BSBCT>(validator, store, nullptr, clock_drift)
    {}

    bool Validate(conse PrePrepare & message, uint8_t remote_delegate_id, ValidationStatus * status)
    {
        using namespace logos;

        if (!_validator.Validate(message, remote_delegate_id))
        {
            UpdateStatusReason(status, process_result::bad_signature);
            return false;
        }

        /// TODO maintain previous/sequence for efficiency
        BatchStateBlock previous;
        if (message.previous != 0 && _store.batch_block_get(message.previous, previous))
        {
            UpdateStatusReason(status, process_result::gap_previous);
            return false;
        }

        if (!ValidateTimestamp(message.timestamp))
        {
            UpdateStatusReason(status, process_result::clock_drift);
            return false;
        }

        if (message.previous != 0 && message.sequence != (previous.sequence + 1))
        {
            UpdateStatusReason(status, process_result::wrong_sequence_number);
            return false;
        }

        return PersistenceManager<BSBCT>::Validate(message, remote_delegate_id, status);
    }

    bool Validate(const PostCommit & message, uint8_t remote_delegate_id)
    {
        return _validator.Validate(message, remote_delegate_id);
    }

    void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id)
    {
        PersistenceManager<BSBCT>::ApplyUpdates(message, delegate_id);
    }
};
