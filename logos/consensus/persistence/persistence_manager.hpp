/// @file
/// This file declares PersistenceManager class which handles validation and persistence of
/// consensus related objects
#pragma once

#include <logos/node/common.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>

class ReservationsProvider;

template<ConsensusType CT>
class PersistenceManager {

protected:

    using Store         = logos::block_store;
    using Request       = RequestMessage<CT>;
    using PrePrepare    = PrePrepareMessage<CT>;

public:
    PersistenceManager(Store & store, ReservationsProvider & reservations);
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
    /// @returns true if validated
    bool Validate(const PrePrepare & message);

    /// Save message to the database
    /// @param message to save [in]
    /// @param delegate_id delegate id [in]
    void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id);
};
