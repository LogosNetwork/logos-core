/// @file
/// This file contains declaration of Request related validation/persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

const ConsensusType R = ConsensusType::Request;

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

template<>
class PersistenceManager<R> : public Persistence
{

    friend class BatchBlockConsensusManager;

protected:

    using Message         = DelegateMessage<R>;
    using PrePrepare      = PrePrepareMessage<R>;
    using ReservationsPtr = std::shared_ptr<ReservationsProvider>;

public:

    PersistenceManager(Store & store,
                       ReservationsPtr reservations,
                       Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT);
    virtual ~PersistenceManager() = default;

    virtual void ApplyUpdates(const ApprovedRB & message, uint8_t delegate_id);

    virtual bool BlockExists(const ApprovedBSB & message);

    bool ValidateRequest(std::shared_ptr<const Request> request,
                         logos::process_return & result,
                         bool allow_duplicates = true,
                         bool prelim = false);
    bool ValidateSingleRequest(std::shared_ptr<const Request> request,
                               logos::process_return & result,
                               bool allow_duplicates = true);
    bool ValidateAndUpdate(std::shared_ptr<const Request> request,
                           logos::process_return & result,
                           bool allow_duplicates = true);

    bool ValidateBatch(const PrePrepare & message, RejectionMap & rejection_map);

    bool Validate(const PrePrepare & message, ValidationStatus * status = nullptr);

    static constexpr uint32_t  RESERVATION_PERIOD  = 2;
    static constexpr uint128_t MIN_TRANSACTION_FEE = 0x21e19e0c9bab2400000_cppui128; // 10^22

private:

    void StoreRequestBlock(const ApprovedRB & message,
                           MDB_txn * transaction,
                           uint8_t delegate_id);

    void ApplyRequestBlock(const ApprovedRB & message,
                           MDB_txn * transaction);
    void ApplyRequest(const Send & request,
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
