#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/log.hpp>

#include <memory>
#include <list>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/mem_fun.hpp>

using boost::multi_index::hashed_non_unique;
using boost::multi_index::const_mem_fun;
using boost::multi_index::indexed_by;
using boost::multi_index::sequenced;

class RequestHandler
{
    using Requests =
            boost::multi_index_container<
                logos::state_block,
                indexed_by<
                    sequenced<>,
                    hashed_non_unique<
                        const_mem_fun<
                            logos::block, BlockHash, &logos::block::hash
                        >
                    >
                >
            >;

public:

    RequestHandler();

    void OnRequest(std::shared_ptr<logos::state_block> block);
    void OnPostCommit(const BatchStateBlock & batch);

    BatchStateBlock & PrepareNextBatch();
    BatchStateBlock & GetCurrentBatch();

    void InsertFront(const std::list<logos::state_block> & blocks);
    void Acquire(const BatchStateBlock & batch);

    void PopFront();
    bool BatchFull();
    bool Empty();

    bool Contains(const BlockHash & hash);

private:

    Log             _log;
    BatchStateBlock _current_batch;
    Requests        _requests;
};
