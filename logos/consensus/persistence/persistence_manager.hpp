#pragma once

#include <logos/blockstore.hpp>
#include <logos/node/common.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

#include <unordered_set>

class PersistenceManager
{

    class DynamicStorage;

    using Store        = logos::block_store;
    using Log          = boost::log::sources::logger_mt;
    using Hash         = logos::block_hash;
    using AccountCache = std::unordered_map<logos::account, logos::account_info>;
    using BlockCache   = std::unordered_set<Hash>;
    using StorageMap   = std::unordered_map<uint8_t, DynamicStorage>;

public:

    PersistenceManager(Store & store,
                       Log & log);

    void ApplyUpdates(const BatchStateBlock & message, uint8_t delegate_id);

    bool Validate(const logos::state_block & block, logos::process_return & result, uint8_t delegate_id);
    bool Validate(const logos::state_block & block, uint8_t delegate_id);

    void ClearCache(uint8_t delegate_id);

private:

    struct DynamicStorage
    {
        DynamicStorage(Store & store)
            : store(store)
        {}

        bool StateBlockExists(const Hash & hash)
        {
            return pending_blocks.find(hash) != pending_blocks.end()
                    || store.state_block_exists(hash);
        }

        bool GetAccount(const logos::account & account, logos::account_info & info)
        {
            if(pending_account_changes.find(account) != pending_account_changes.end())
            {
                info = pending_account_changes[account];
                return false;
            }

            return store.account_get(account, info);
        }

        void ClearCache()
        {
            pending_blocks.clear();
            pending_account_changes.clear();
        }

        // Contains blocks validated by this delegate
        // but not yet confirmed via consensus.
        BlockCache   pending_blocks;
        AccountCache pending_account_changes;
        Store &      store;
    };

    void StoreBatchMessage(const BatchStateBlock & message, MDB_txn * transaction);
    void ApplyBatchMessage(const BatchStateBlock & message, uint8_t delegate_id, MDB_txn * transaction);

    void ApplyStateMessage(const logos::state_block & block, MDB_txn * transaction);

    bool UpdateSourceState(const logos::state_block & block, MDB_txn * transaction);
    void UpdateDestinationState(const logos::state_block & block, MDB_txn * transaction);

    DynamicStorage & GetStore(uint8_t delegate_id);


    StorageMap _dynamic_storage;
    std::mutex _dynamic_storage_mutex;
    Store &    _store;
    Log &      _log;
};


