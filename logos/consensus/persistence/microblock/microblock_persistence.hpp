
/// @file
/// This file contains declaration of MicroBlock related validation and persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

static constexpr ConsensusType MBCT = ConsensusType::MicroBlock;

class ReservationsProvider;

template<>
class PersistenceManager<MBCT> : public Persistence {

protected:

    using BatchTips                     = BlockHash[NUM_DELEGATES];
    using IteratorBatchBlockReceiverCb  = std::function<void(uint8_t, const BatchStateBlock&)>;
    using BatchBlockReceiverCb          = std::function<void(const BatchStateBlock&)>;
    using Request                       = RequestMessage<MBCT>;
    using PrePrepare                    = PrePrepareMessage<MBCT>;
    using ReservationsPtr               = std::shared_ptr<ReservationsProvider>;

public:
    PersistenceManager(MessageValidator & validator,
                       Store & store,
                       ReservationsPtr,
                       Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT);
    virtual ~PersistenceManager() = default;

    /// Request validation, EDDSA signature and block validation
    /// @param block request to validate [in]
    /// @param result of validation [in]
    /// @param allow_duplicate allow duplicate request [in]
    /// @returns true if validated
    virtual bool Validate(const Request & block, logos::process_return & result, bool allow_duplicate = true)
    { return true; }
    virtual bool Validate(const Request & block)
    { return true; }

    /// Backup delegate validation
    /// @param message to validate [in]
    /// @param remote_delegate_id remote delegate id [in]
    /// @param status result of the validation, optional [in|out]
    /// @returns true if validated
    virtual bool Validate(const PrePrepare & message, uint8_t remote_delegate_id, ValidationStatus * status = nullptr);

    /// Commit PrePrepare to the database
    /// @param message to commit [in]
    /// @param delegate_id delegate id [in]
    virtual void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id);
    virtual void ApplyUpdates(const PrePrepare & message)
    {
        ApplyUpdates(message, 0);
    }

protected:

};
