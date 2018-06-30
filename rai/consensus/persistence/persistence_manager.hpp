#pragma once

#include <rai/blockstore.hpp>

class PersistenceManager
{

    using Store = rai::block_store;

public:

    PersistenceManager(Store & store);

    void StoreBatchMessage(const BatchStateBlock & message);

    bool Validate(const rai::state_block & block);

private:

    Store & _store;
};


