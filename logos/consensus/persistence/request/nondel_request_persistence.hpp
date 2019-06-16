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

    bool ValidatePreprepare(const PrePrepare & message, ValidationStatus * status) override
    {
        using namespace logos;

        if (!status || status->progress < RVP_DRIFT)
        {
            if(_clock_drift > Milliseconds(0))
            {
                if (!ValidateTimestamp(message.timestamp))
                {
                    UpdateStatusReason(status, logos::process_result::clock_drift);
                    return false;
                }
            }

            if (status)
                status->progress = RVP_DRIFT;
        }

        if (!status || status->progress < RVP_PREVIOUS)
        {
            ApprovedRB previous;
            if ((!message.previous.is_zero()) && _store.request_block_get(message.previous, previous))
            {
                UpdateStatusReason(status, logos::process_result::gap_previous);
                return false;
            }

            //have previous now
            if(previous.epoch_number > 0)
            {
                if( ! ((previous.epoch_number == message.epoch_number) && (message.sequence == (previous.sequence + 1))) &&
                        ! (((previous.epoch_number+1) == message.epoch_number) && (message.sequence == 0)))
                {
                    LOG_TRACE(_logger) << "NonDelPersistenceManager<R>::"<< __func__ << ":wrong_sequence_number:"
                            << " previous=" << previous.epoch_number << ":" << previous.sequence
                            << " verifiee=" << message.epoch_number << ":" << message.sequence;

                    UpdateStatusReason(status, logos::process_result::wrong_sequence_number);
                    return false;
                }
            }

            if (status)
                status->progress = RVP_PREVIOUS;
        }

        return PersistenceManager<R>::Validate(message, status);
    }

    bool ValidateSingleRequest(std::shared_ptr<const Request> block, uint32_t cur_epoch_num, logos::process_return &result, bool allow_duplicate=false) override
    {
        if(block->origin.is_zero())
        {
            result.code = logos::process_result::opened_burn_account;
            return false;
        }

        if(block->fee.number() < MIN_TRANSACTION_FEE)
        {
            result.code = logos::process_result::insufficient_fee;
            return false;
        }

        return PersistenceManager<R>::ValidateSingleRequest(block, cur_epoch_num, result, allow_duplicate);
    }

    bool ValidateSingleRequest(const std::shared_ptr<Request> block, uint32_t cur_epoch_num)
    {
       logos::process_return res;
       return ValidateSingleRequest(block, cur_epoch_num, res);
    }
};
