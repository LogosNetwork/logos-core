#pragma once

#include <logos/node/common.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/blockstore.hpp>
#include <logos/lib/log.hpp>

#include <unordered_set>

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

class PersistenceManager
{

    class Reservations;

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

    struct Reservations
    {
        using AccountCache = std::unordered_map<logos::account, logos::account_info>;

        Reservations(Store & store)
            : store(store)
        {}

        //-------------------------------------------------------------------------
        // XXX - It is possible for a delegate D1 that has validated/Post-Comitted
        //       (but hasn't yet updated its database and cleared the reservation)
        //       a send request from account A1 to receive the subsequent request
        //       from account A1 as a backup delegate for a PrePrepare from another
        //       delegate D2. In this case D1 would reject a valid send transaction
        //       from A1 since A1 would appear to still be reserved. However, this
        //       is unlikely, as for this to occur, the Post-Commit would have to
        //       propagate to both D2 and to the client before D1 clears the
        //       reservation. When this occurs, D1 will attempt to Acquire an
        //       account that is already stored in the Reservations cache. However,
        //       this is not the only case in which a cached account will be
        //       acquired.
        //-------------------------------------------------------------------------
        bool Acquire(const logos::account & account, logos::account_info & info)
        {
            std::lock_guard<std::mutex> lock(mutex);

            if(accounts.find(account) != accounts.end())
            {
                LOG_WARN(log) << "Reservations::Acquire - Warning - attempt to "
                               << "acquire account "
                               << account.to_string()
                               << " which is already in the Reservations cache.";

                info = accounts[account];
                return false;
            }

            auto ret = store.account_get(account, info);

            if(!ret)
            {
                accounts[account] = info;
            }

            return ret;
        }

        void Release(const logos::account & account)
        {
            std::lock_guard<std::mutex> lock(mutex);
            accounts.erase(account);
        }

        AccountCache accounts;
        Store &      store;
        Log          log;
        std::mutex   mutex;
    };

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


