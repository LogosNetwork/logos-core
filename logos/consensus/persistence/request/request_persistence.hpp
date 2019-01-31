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

protected:

    using Request         = DelegateMessage<R>;
    using PrePrepare      = PrePrepareMessage<R>;
    using ReservationsPtr = std::shared_ptr<ReservationsProvider>;

public:

    PersistenceManager(Store & store,
                       ReservationsPtr reservations,
                       Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT);
    virtual ~PersistenceManager() = default;

    virtual void ApplyUpdates(const ApprovedBSB & message, uint8_t delegate_id);

    virtual bool Validate(const Request & request, logos::process_return & result, bool allow_duplicates = true);
    virtual bool Validate(const Request & request);

    virtual bool Validate(const PrePrepare & message, ValidationStatus * status = nullptr);

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

    static constexpr uint32_t  RESERVATION_PERIOD  = 2;
    static constexpr uint128_t MIN_TRANSACTION_FEE = 0x21e19e0c9bab2400000_cppui128; // 10^22

    Log             _log;
    ReservationsPtr _reservations;
    std::mutex      _reservation_mutex;
    std::mutex      _destination_mutex;
};
