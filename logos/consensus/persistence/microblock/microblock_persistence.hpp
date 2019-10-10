
/// @file
/// This file contains declaration of MicroBlock related validation and persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

static constexpr ConsensusType MBCT = ConsensusType::MicroBlock;

class Reservations;

template<>
class PersistenceManager<MBCT> : public Persistence
{

protected:

    using Message                      = DelegateMessage<MBCT>;
    using PrePrepare                   = PrePrepareMessage<MBCT>;
    using ReservationsPtr              = std::shared_ptr<Reservations>;
    using IteratorBatchBlockReceiverCb = std::function<void(uint8_t, const ApprovedRB &)>;
    using BatchBlockReceiverCb         = std::function<void(const ApprovedRB &)>;

    enum microblock_validation_progress
    {
        MVP_BEGIN,      /* initial state, validation not started */
        MVP_DRIFT,      /* timestamp drift validated */
        MVP_PREVIOUS,   /* previous microblock found */
        MVP_TIPS_FIRST, /* validation of request tips started, some not found */
        MVP_TIPS_DONE,  /* all request tips found */
        MVP_END         /* final state, validation OK */
    };

public:
    PersistenceManager(Store & store,
                       ReservationsPtr,
                       Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT)
    : Persistence(store, clock_drift)
    {}

    virtual ~PersistenceManager() = default;

    /// Request validation, EDDSA signature and block validation
    /// @param block request to validate [in]
    /// @param result of validation [in]
    /// @param allow_duplicate allow duplicate request [in]
    /// @returns true if validated
    virtual bool Validate(const Message & block, logos::process_return & result, bool allow_duplicate = true)
    { return true; }
    virtual bool Validate(const Message & block)
    { return true; }

    /// Backup delegate validation
    /// @param message to validate [in]
    /// @param status result of the validation, optional [in|out]
    /// @returns true if validated
    virtual bool Validate(const PrePrepare & message, ValidationStatus * status = nullptr);

    /// Commit PrePrepare to the database
    /// @param message to commit [in]
    /// @param delegate_id delegate id [in]
    virtual void ApplyUpdates(const ApprovedMB & block, uint8_t delegate_id);
    virtual void ApplyUpdates(const ApprovedMB & block)
    {
        ApplyUpdates(block, 0);
    }

    virtual bool BlockExists(const ApprovedMB & message);
};
