/// @file
/// This file contains declaration of Epoch related validation and persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

static constexpr ConsensusType ECT = ConsensusType::Epoch;

class ReservationsProvider;

template<>
class PersistenceManager<ECT> : public Persistence
{

protected:

    using RequestBlockTips = BlockHash[NUM_DELEGATES];
    using Message          = DelegateMessage<ECT>;
    using PrePrepare       = PrePrepareMessage<ECT>;
    using ReservationsPtr  = std::shared_ptr<ReservationsProvider>;

public:

    PersistenceManager(Store & store,
                       ReservationsPtr,
                       Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT);
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
    virtual void ApplyUpdates(const ApprovedEB & block, uint8_t delegate_id);
    virtual void ApplyUpdates(const ApprovedEB & block)
    {
        ApplyUpdates(block, 0);
    }

    void GetEpochFirstRBs(uint32_t epoch_number, RequestBlockTips & epoch_firsts);
    void LinkAndUpdateTips(uint8_t delegate, uint32_t epoch_number, const BlockHash & first_request_block, MDB_txn *transaction);

    virtual bool BlockExists(const ApprovedEB & message);
};
