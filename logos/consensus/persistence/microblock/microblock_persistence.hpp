
/// @file
/// This file contains declaration of MicroBlock related validation and persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

static constexpr ConsensusType MBCT = ConsensusType::MicroBlock;

class ReservationsProvider;

template<>
class PersistenceManager<MBCT> {

protected:

    using BatchTips                     = BlockHash[NUM_DELEGATES];
    using IteratorBatchBlockReceiverCb  = std::function<void(uint8_t, const BatchStateBlock&)>;
    using BatchBlockReceiverCb          = std::function<void(const BatchStateBlock&)>;
    using Store                         = logos::block_store;
    using Request                       = RequestMessage<MBCT>;
    using PrePrepare                    = PrePrepareMessage<MBCT>;
    using ReservationsPtr               = std::shared_ptr<ReservationsProvider>;

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
    /// Iterates each delegates' batch state block chain.
    /// @param start tips to start iteration [in]
    /// @param end tips to end iteration [in]
    /// @param cb function to call for each delegate's batch state block, the function's argument are
    ///   delegate id and BatchStateBlock
    void BatchBlocksIterator(const BatchTips &start, const BatchTips &end, IteratorBatchBlockReceiverCb cb);

    Store &     _store;
    Log         _log;
};