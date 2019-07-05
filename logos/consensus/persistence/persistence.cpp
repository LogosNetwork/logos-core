/// @file
/// This file implements base Persistence class

#include <logos/consensus/persistence/persistence.hpp>
#include <logos/consensus/message_validator.hpp>

Persistence::Milliseconds constexpr Persistence::DEFAULT_CLOCK_DRIFT;
Persistence::Milliseconds constexpr Persistence::ZERO_CLOCK_DRIFT;

// TODO: Discuss total order of receives in
//       receive_db of all nodes.
void Persistence::PlaceReceive(ReceiveBlock & receive,
                               uint64_t timestamp,
                               MDB_txn * transaction)
{
    ReceiveBlock prev;
    ReceiveBlock cur;

    auto hash = receive.Hash();
    uint64_t timestamp_a = timestamp;

    if(!_store.receive_get(receive.previous, cur, transaction))
    {
        bool epoch_generated_cur = false;

        auto get_timestamp = [&](const ReceiveBlock receive)
        {
            if(_store.request_exists(receive.source_hash))
            {
                epoch_generated_cur = false;

                std::shared_ptr<Request> request;
                if(_store.request_get(receive.source_hash, request, transaction))
                {
                    LOG_FATAL(_log) << "Persistence::PlaceReceive - "
                                    << "Failed to get a previous request with hash: "
                                    << receive.source_hash.to_string();
                    trace_and_halt();
                }

                ApprovedRB approved;

                if(_store.request_block_get(request->locator.hash, approved, transaction))
                {
                    return uint64_t(0);
                }

                return approved.timestamp;
            }
            else
            {
                epoch_generated_cur = true;

                ApprovedEB epoch;
                if(_store.epoch_get(receive.source_hash, epoch, transaction))
                {
                    LOG_FATAL(_log) << "Persistence::PlaceReceive - "
                                    << "Failed to get a previous epoch block with hash: "
                                    << receive.source_hash.to_string();
                    trace_and_halt();
                }

                return epoch.timestamp;
            }
        };

        // Returns true if 'a' should precede 'b'
        // in the receive chain.
        auto receive_cmp = [&](const ReceiveBlock & a,
                               const ReceiveBlock & b)
        {
            // need b's timestamp
            auto timestamp_b = get_timestamp(b);

            bool a_is_less;
            if(timestamp_a != timestamp_b)
            {
                a_is_less = timestamp_a < timestamp_b;
            }
            else
            {
                a_is_less = a.Hash() < b.Hash();
            }

            // update for next compare if needed
            timestamp_a = timestamp_b;

            return a_is_less;
        };

        bool epoch_generated_prev = false;

        while(receive_cmp(receive, cur))
        {
            prev = cur;
            epoch_generated_prev = epoch_generated_cur;

            if(_store.receive_get(cur.previous,
                                  cur,
                                  transaction))
            {
                if(!cur.previous.is_zero())
                {
                    LOG_FATAL(_log) << "Persistence::PlaceReceive - "
                                    << "Failed to get a previous receive block with hash: "
                                    << cur.previous.to_string();
                    trace_and_halt();
                }
                break;
            }
        }

        // SYL integration fix: we only want to modify prev in DB if we are inserting somewhere in the middle of the receive chain
        if(!prev.source_hash.is_zero())
        {
            if(!epoch_generated_prev)
            {
                std::shared_ptr<Request> prev_request;
                if(_store.request_get(prev.source_hash, prev_request, transaction))
                {
                    LOG_FATAL(_log) << "Persistence::PlaceReceive - "
                                    << "Failed to get a previous request with hash: "
                                    << prev.source_hash.to_string();
                    trace_and_halt();
                }

                if(prev_request->origin.is_zero())  // sending to burn address is already prohibited
                {
                    LOG_FATAL(_log) << "Persistence::PlaceReceive - "
                                    << "Encountered request with empty account field, hash: "
                                    << prev.source_hash.to_string();
                    trace_and_halt();
                }
            }

            // point following receive aka prev's 'previous' field to new receive
            receive.previous = prev.previous;
            prev.previous = hash;
            auto prev_hash (prev.Hash());

            if(_store.receive_put(prev_hash, prev, transaction))
            {
                LOG_FATAL(_log) << "Persistence::PlaceReceive - "
                                << "Failed to store receive block with hash: "
                                << prev_hash.to_string();

                trace_and_halt();
            }
        }
    }
    else if (!receive.previous.is_zero())
    {
        LOG_FATAL(_log) << "Persistence::PlaceReceive - "
                        << "Failed to get a previous receive block with hash: "
                        << receive.previous.to_string();
        trace_and_halt();
    }

    if(_store.receive_put(hash, receive, transaction))
    {
        LOG_FATAL(_log) << "Persistence::PlaceReceive - "
                        << "Failed to store receive block with hash: "
                        << hash.to_string();

        trace_and_halt();
    }
}
