/// @file
/// This file declares batch state block non-delegate persistence manager

#pragma once

#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>
#include <logos/consensus/persistence/nondel_persistence_manager.hpp>
#include <logos/consensus/persistence/validator_builder.hpp>
#include <logos/consensus/persistence/nondel_persistence.hpp>

template<>
class NonDelPersistenceManager<BSBCT> : public PersistenceManager<BSBCT>, public NoneDelegatePersistence<BSBCT>
{
public:
    using PersistenceManager<BSBCT>::Validate;

    NonDelPersistenceManager(Store &store,
                             Milliseconds clock_drift = ZERO_CLOCK_DRIFT)
        : PersistenceManager<BSBCT>(store, nullptr, clock_drift)
        , NoneDelegatePersistence<BSBCT>(store)
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

        return PersistenceManager<BSBCT>::Validate(message, status);
    }

    bool Validate(const request & block, logos::process_return &result, bool allow_duplicate=false) override
    {
        if(block.account.is_zero())
        {
            result.code = logos::process_result::opened_burn_account;
            return false;
        }

        if(block.transaction_fee.number() < MIN_TRANSACTION_FEE)
        {
            result.code = logos::process_result::insufficient_fee;
            return false;
        }

        return PersistenceManager<BSBCT>::Validate(block, result, allow_duplidate);
    }

    bool Validate(const request & block) override
    {
       logos::process_return res;
       return Validate(block, res);
    }
};
