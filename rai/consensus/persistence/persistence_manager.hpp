#pragma once

#include <rai/blockstore.hpp>
#include <rai/node/common.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

#include <unordered_set>

class PersistenceManager
{

    using Store        = rai::block_store;
    using Log          = boost::log::sources::logger_mt;
    using Hash         = rai::block_hash;
    using AccountCache = std::unordered_map<rai::account, rai::account_info>;
    using BlockCache   = std::unordered_set<Hash>;

public:

    PersistenceManager(Store & store,
                       Log & log);

    void StoreBatchMessage(const BatchStateBlock & message);
    void ApplyBatchMessage(const BatchStateBlock & message, uint8_t delegate_id);

    bool Validate(const rai::state_block & block, rai::process_return & result);
    bool Validate(const rai::state_block & block);

    void ClearCache();

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

        bool GetAccount(const rai::account & account, rai::account_info & info)
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

    void ApplyStateMessage(const rai::state_block & block);

    bool UpdateSourceState(const rai::state_block & block);
    void UpdateDestinationState(const rai::state_block & block);


    DynamicStorage _store;
    Log &          _log;
};


