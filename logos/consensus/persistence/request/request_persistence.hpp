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

    using Message         = DelegateMessage<R>;
    using PrePrepare      = PrePrepareMessage<R>;
    using ReservationsPtr = std::shared_ptr<ReservationsProvider>;
    using RequestPtr      = std::shared_ptr<const Request>;

public:

    PersistenceManager(Store & store,
                       ReservationsPtr reservations,
                       Milliseconds clock_drift = DEFAULT_CLOCK_DRIFT);
    virtual ~PersistenceManager() = default;

    virtual void ApplyUpdates(const ApprovedRB & message, uint8_t delegate_id);

    virtual bool Validate(RequestPtr request,
                          logos::process_return & result,
                          uint32_t cur_epoch_num,
                          bool allow_duplicates = true);
    virtual bool Validate(RequestPtr request,uint32_t cur_epoch_num);

    virtual bool Validate(const PrePrepare & message, ValidationStatus * status = nullptr);

private:

    void StoreRequestBlock(const ApprovedRB & message,
                           MDB_txn * transaction,
                           uint8_t delegate_id);

    void ApplyRequestBlock(const ApprovedRB & message,
                           MDB_txn * transaction);
    void ApplyRequest(RequestPtr request,
                      uint64_t timestamp,
                      MDB_txn * transaction,
                      uint32_t cur_epoch_num);

    template<typename SendType>
    void ApplySend(std::shared_ptr<const SendType> request,
                   uint64_t timestamp,
                   MDB_txn *transaction,
                   BlockHash token_id = 0);

    template<typename AmountType>
    void ApplySend(const Transaction<AmountType> &send,
                   uint64_t timestamp,
                   MDB_txn *transaction,
                   const BlockHash &request_hash,
                   const BlockHash &token_id,
                   uint16_t transaction_index = 0);

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
