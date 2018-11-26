/// @file
/// This file declares PersistenceManager class which handles validation and persistence of
/// consensus related objects
#pragma once

#include <logos/consensus/persistence/persistence.hpp>

#include <logos/node/common.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>

#include <unordered_map>

class ReservationsProvider;
class MessageValidator;

template<ConsensusType CT>
class PersistenceManager {

protected:

    using Store         = logos::block_store;
    using Request       = RequestMessage<CT>;
    using PrePrepare    = PrePrepareMessage<CT>;
    using Milliseconds  = std::chrono::milliseconds;

public:
    PersistenceManager(MessageValidator & validator,
                       Store & store,
                       std::shared_ptr<ReservationsProvider> reservations,
                       Milliseconds clock_drift = Persistence::DEFAULT_CLOCK_DRIFT);
    virtual ~PersistenceManager() = default;

    /// Request validation, EDDSA signature and block validation
    /// @param block request to validate [in]
    /// @param result of validation [in]
    /// @param allow_duplicate allow duplicate request [in]
    /// @returns true if validated
    bool Validate(const Request & block, logos::process_return & result, bool allow_duplicate = true);
    bool Validate(const Request & block);

    /// Message validation
    /// @param message to validate [in]
    /// @param remote_delegate_id remote delegate id [in]
    /// @param status result of the validation, optional [in|out]
    /// @returns true if validated
    bool Validate(const PrePrepare & message, uint8_t remote_delegate_id, ValidationStatus * status = nullptr);

    /// Save message to the database
    /// @param message to save [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id);
};
