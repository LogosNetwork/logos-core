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
    using RequestPtr = std::shared_ptr<Request>;
    using Requests   =
            boost::multi_index_container<
                RequestPtr,
                indexed_by<
                    sequenced<>,
                    hashed_non_unique<
                        const_mem_fun<
                            Request, BlockHash, &Request::GetHash
                        >
                    >
                >
            >;

public:

    using PrePrepare = PrePrepareMessage<ConsensusType::Request>;

    RequestHandler();

    void OnRequest(RequestPtr request);
    void OnPostCommit(const RequestBlock & block);

    PrePrepare & PrepareNextBatch();
    PrePrepare & GetCurrentBatch();

    void InsertFront(const std::list<RequestPtr> & requests);
    void Acquire(const PrePrepare & batch);

    void PopFront();
    bool BatchFull();
    bool Empty();

    bool Contains(const BlockHash & hash);

private:

    Log        _log;
    PrePrepare _current_batch;
    Requests   _requests;
};
