/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

const ConsensusType B = ConsensusType::Request;

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

template<>
class PersistenceManager<B> : public Persistence {

    friend class BatchBlockConsensusManager;

protected:

    using Request         = RequestMessage<B>;
    using PrePrepare      = PrePrepareMessage<B>;
    using ReservationsPtr = std::shared_ptr<ReservationsProvider>;

public:

    PersistenceManager(Store & store,
                       ReservationsPtr reservations,
                       Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT);
    virtual ~PersistenceManager() = default;

    virtual void ApplyUpdates(const ApprovedBSB & message, uint8_t delegate_id);

    virtual bool BlockExists(const ApprovedBSB & message);

    bool ValidateRequest(const Request & block, logos::process_return & result, bool allow_duplicates = true, bool prelim = false);
    virtual bool ValidateSingleRequest(const Request & block, logos::process_return & result, bool allow_duplicates = true);
    bool ValidateAndUpdate(const Request & block, logos::process_return & result, bool allow_duplicates = true);
    bool ValidateBatch(const PrePrepare & message, RejectionMap & rejection_map);

    bool Validate(const PrePrepare & message, ValidationStatus * status = nullptr);

    static constexpr uint32_t  RESERVATION_PERIOD  = 2;
    static constexpr uint128_t MIN_TRANSACTION_FEE = 0x21e19e0c9bab2400000_cppui128; // 10^22

private:

    void StoreBatchMessage(const ApprovedBSB & message,
                           MDB_txn * transaction,
                           uint8_t delegate_id);

    void ApplyBatchMessage(const ApprovedBSB & message,
                           MDB_txn * transaction);
    void ApplyStateMessage(const Send & request,
                           uint64_t timestamp,
                           MDB_txn * transaction);

    bool UpdateSourceState(const Send & request,
                           MDB_txn * transaction);
    void UpdateDestinationState(const Send & request,
                                uint64_t timestamp,
                                MDB_txn * transaction);

    void PlaceReceive(ReceiveBlock & receive,
                      uint64_t timestamp,
                      MDB_txn * transaction);

    Log                 _log;
    ReservationsPtr     _reservations;
    std::mutex          _write_mutex;
};
