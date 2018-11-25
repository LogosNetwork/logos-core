/// @file
/// This file contains declaration of Epoch related validation and persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

static constexpr ConsensusType ECT = ConsensusType::Epoch;

class ReservationsProvider;

template<>
class PersistenceManager<ECT> {

protected:

    using Store             = logos::block_store;
    using Request           = RequestMessage<ECT>;
    using PrePrepare        = PrePrepareMessage<ECT>;
    using ReservationsPtr   = std::shared_ptr<ReservationsProvider>;

public:
    PersistenceManager(Store & store, ReservationsPtr);
    PersistenceManager(Store & store);
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
    /// @returns true if validated
    virtual bool Validate(const PrePrepare & message);

    /// Commit PrePrepare to the database
    /// @param message to commit [in]
    /// @param delegate_id delegate id [in]
    virtual void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id);
    virtual void ApplyUpdates(const PrePrepare & message)
    {
        ApplyUpdates(message, 0);
    }

protected:

    Store &     _store;
    Log         _log;
};