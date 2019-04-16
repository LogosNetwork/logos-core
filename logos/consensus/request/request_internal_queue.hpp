#pragma once

#include <memory>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

#include <logos/consensus/message_handler.hpp>
#include <logos/request/requests.hpp>
#include <logos/lib/log.hpp>
#include <logos/lib/trace.hpp>

using boost::multi_index::const_mem_fun;
using boost::multi_index::hashed_non_unique;
using boost::multi_index::indexed_by;
using boost::multi_index::sequenced;

class RequestMessageHandler;

class RequestInternalQueue {
    friend class RequestConsensusManager;

    friend class RequestMessageHandler;

    using PrePrepare = PrePrepareMessage<ConsensusType::Request>;
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

private:

    /// Checks if the manager's internal request queue is empty.
    ///
    ///     @return true if empty false otherwise
    bool Contains(const BlockHash &);
    bool Empty();
    void PushBack(const RequestPtr);
    void InsertFront(const std::list<RequestPtr> &);
    void PopFront(const PrePrepare &);

    Requests _requests;
    Log      _log;
};
