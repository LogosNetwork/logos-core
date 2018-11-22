#pragma once

#include <logos/consensus/persistence/reservations.hpp>
#include <logos/node/common.hpp>
#include <logos/lib/blocks.hpp>
//#include <logos/blockstore.hpp>
//#include <logos/lib/log.hpp>

#include <unordered_set>

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

class PersistenceManager
{
    using Store        = logos::block_store;
    using Hash         = logos::block_hash;
    using BlockCache   = std::unordered_set<Hash>;

public:

    PersistenceManager(Store & store);

    void ApplyUpdates(const BatchStateBlock & message, uint8_t delegate_id);

    bool Validate(const logos::state_block & block,
                  logos::process_return & result,
                  bool allow_duplicates = true);
    bool Validate(const logos::state_block & block);

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


    Reservations _reservations;
    std::mutex   _reservation_mutex;
    std::mutex   _destination_mutex;
    Store &      _store;
    Log          _log;
};


