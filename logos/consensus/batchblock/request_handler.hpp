#pragma once

#include <logos/consensus/messages/messages.hpp>
#include <logos/lib/blocks.hpp>
#include <logos/lib/log.hpp>
#include <logos/consensus/persistence/batchblock/batchblock_persistence.hpp>

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
                Send,
                indexed_by<
                    sequenced<>,
                    hashed_non_unique<
                        const_mem_fun<
                            Send, BlockHash, &Send::GetHash
                        >
                    >
                >
            >;

public:

    using PrePrepare = PrePrepareMessage<ConsensusType::Request>;
    using Request    = RequestMessage<ConsensusType::Request>;
    using Manager    = PersistenceManager<ConsensusType::Request>;

    RequestHandler();

    void OnRequest(std::shared_ptr<Send> request);
    void OnPostCommit(const RequestBlock & block);

    PrePrepare & PrepareNextBatch(Manager & manager, bool repropose = false);
    PrePrepare & GetCurrentBatch();
    void InsertFront(const std::list<Send> & requests);
    void Acquire(const PrePrepare & batch);

    void PopFront();
    bool BatchFull();
    bool Empty();

    bool Contains(const BlockHash & hash);

private:

    std::mutex _mutex;
    Log        _log;
    PrePrepare _current_batch;
    Requests   _requests;
};
