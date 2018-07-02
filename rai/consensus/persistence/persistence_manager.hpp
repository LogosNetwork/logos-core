#pragma once

#include <rai/blockstore.hpp>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/logger.hpp>

class PersistenceManager
{

    using Store = rai::block_store;
    using Log   = boost::log::sources::logger_mt;

public:

    PersistenceManager(Store & store,
                       Log & log);

    void StoreBatchMessage(const BatchStateBlock & message);
    void ApplyBatchMessage(const BatchStateBlock & message);

    bool Validate(const rai::state_block & block);

private:

    void ApplyStateMessage(const rai::state_block & block);

    bool UpdateSourceState(const rai::state_block & block, rai::amount & quantity);
    void UpdateDestinationState(const rai::state_block & block, rai::amount quantity);

    Store & _store;
    Log &   _log;
};


