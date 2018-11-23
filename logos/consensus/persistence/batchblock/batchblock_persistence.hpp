/// @file
/// This file contains declaration of BatchStateBlock related validation/persistence

#pragma once

#include <logos/consensus/persistence/persistence_manager.hpp>

const ConsensusType BSBCT = ConsensusType::BatchStateBlock;

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

template<>
class PersistenceManager<BSBCT> {
    using Store             = logos::block_store;
    using Hash              = logos::block_hash;
    using Request           = RequestMessage<BSBCT>;
    using PrePrepare        = PrePrepareMessage<BSBCT>;
    using ReservationsPtr   = std::shared_ptr<ReservationsProvider>;

public:

    PersistenceManager(Store & store, ReservationsPtr reservations);
    virtual ~PersistenceManager() = default;

    virtual void ApplyUpdates(const PrePrepare & message, uint8_t delegate_id);

    virtual bool Validate(const Request & block,
                  logos::process_return & result,
                  bool allow_duplicates = true);
    virtual bool Validate(const Request & block);

    virtual bool Validate(const PrePrepare & message);

private:

    void StoreBatchMessage(const BatchStateBlock & message,
                           MDB_txn * transaction,
                           uint8_t delegate_id);

    void ApplyBatchMessage(const BatchStateBlock & message,
                           MDB_txn * transaction);
    void ApplyStateMessage(const logos::state_block & block,
                           uint64_t timestamp,
                           MDB_txn * transaction);

    bool UpdateSourceState(const logos::state_block & block,
                           MDB_txn * transaction);
    void UpdateDestinationState(const logos::state_block & block,
                                uint64_t timestamp,
                                MDB_txn * transaction);

    void PlaceReceive(logos::state_block & receive,
                      MDB_txn * transaction);

    static constexpr uint64_t  RESERVATION_PERIOD  = 2;
    static constexpr uint128_t MIN_TRANSACTION_FEE = 0x21e19e0c9bab2400000_cppui128; // 10^22


    Store &             _store;
    Log                 _log;
    ReservationsPtr     _reservations;
    std::mutex          _reservation_mutex;
    std::mutex          _destination_mutex;
};
