/// @file
/// This file contains declaration of Epoch related validation and persistence

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/lib/trace.hpp>

PersistenceManager<ECT>::PersistenceManager(Store & store,
                                            ReservationsPtr,
                                            Milliseconds clock_drift)
    : Persistence(store, clock_drift)
{}

bool
PersistenceManager<ECT>::Validate(
    const PrePrepare & epoch,
    ValidationStatus * status)
{
    BlockHash previous_epoch_hash;
    ApprovedEB previous_epoch;
    using namespace logos;

    if (epoch.primary_delegate >= NUM_DELEGATES)
    {
        UpdateStatusReason(status, process_result::invalid_request);
        LOG_ERROR(_log) << "PersistenceManager::Validate primary index out of range " << (int) epoch.primary_delegate;
        return false;
    }

    if (_store.epoch_tip_get(previous_epoch_hash))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate failed to get epoch tip";
        trace_and_halt();
    }

    if (_store.epoch_get(previous_epoch_hash, previous_epoch))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate failed to get epoch: " <<
                        previous_epoch_hash.to_string();
        UpdateStatusReason(status, process_result::gap_previous);
        return false;
    }

    // verify epoch number = previous + 1
    if (epoch.epoch_number != (previous_epoch.epoch_number + 1))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate account invalid epoch number " <<
                        epoch.epoch_number << " " << previous_epoch.epoch_number;
        UpdateStatusReason(status, process_result::block_position);
        return false;
    }

    // verify microblock tip exists
    BlockHash micro_block_tip;
    if (_store.micro_block_tip_get(micro_block_tip))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate failed to get microblock tip";
        trace_and_halt();
    }

    if (_store.micro_block_tip_get(micro_block_tip))
    {
        LOG_FATAL(_log) << "PersistenceManager::Validate micro block tip doesn't exist";
        trace_and_halt();
        return false;
    }

    if (epoch.micro_block_tip != micro_block_tip)
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate previous micro block doesn't exist " <<
                        epoch.micro_block_tip.to_string() << " " << micro_block_tip.to_string();
        UpdateStatusReason(status, process_result::invalid_tip);
        return false;
    }

    EpochVotingManager voting_mgr(_store);
    //epoch block has epoch_number 1 less than current epoch, so +1
    if (!voting_mgr.ValidateEpochDelegates(epoch.delegates, epoch.epoch_number + 1))
    {
        LOG_ERROR(_log) << "PersistenceManager::Validate invalid delegates ";
        UpdateStatusReason(status, process_result::not_delegate);
        return false;
    }

    // verify transaction fee pool? TBD
    LOG_WARN(_log) << "PersistenceManager::Validate  WARNING TRANSACTION POOL IS NOT VALIDATED";

    return true;
}

void
PersistenceManager<ECT>::ApplyUpdates(
    const ApprovedEB & block,
    uint8_t)
{
    LOG_INFO(_log) << "Applying updates for Epoch";
    logos::transaction transaction(_store.environment, nullptr, true);

    // See comments in request_persistence.cpp
    if (BlockExists(block))
    {
        LOG_DEBUG(_log) << "PersistenceManager<ECT>::ApplyUpdates - epoch already exists, ignoring";
        return;
    }

    BlockHash epoch_hash = block.Hash();
    bool transition = !BlockExists(block);

    if(_store.epoch_put(block, transaction) || _store.epoch_tip_put(epoch_hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to store epoch or epoch tip "
                                << epoch_hash.to_string();
        trace_and_halt();
    }

    //The epoch number in the epoch block is one less than the current epoch
    if(transition)
    {
        TransitionNextEpoch(transaction, block.epoch_number+1);
    }

    if(_store.consensus_block_update_next(block.previous, epoch_hash, ConsensusType::Epoch, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::ApplyUpdates failed to get previous block "
                        << block.previous.to_string();
        trace_and_halt();
    }

    // Link epoch's first request block with previous epoch's last request block
    // starting from epoch 3 (i.e. after Genesis)
    if (block.epoch_number <= GENESIS_EPOCH)
    {
        return;
    }

    BatchTips cur_e_first;
    auto cur_epoch_number (block.epoch_number + 1);
    _store.GetEpochFirstRBs(cur_epoch_number, cur_e_first);

    for (uint8_t delegate = 0; delegate < NUM_DELEGATES; ++delegate)
    {
        LinkAndUpdateTips(delegate, cur_epoch_number, cur_e_first[delegate], transaction);
    }
}

void
PersistenceManager<ECT>::LinkAndUpdateTips(
    uint8_t delegate,
    uint32_t epoch_number,
    const BlockHash & first_request_block,
    MDB_txn *transaction)
{
    // Get previous epoch's request block tip
    BlockHash prev_e_last;
    if (_store.request_tip_get(delegate, epoch_number - 1, prev_e_last))
    {
        LOG_FATAL(_log) << "PersistenceManager<ECT>::LinkAndUpdateTips failed to get request block tip for delegate "
                        << std::to_string(delegate) << " for epoch number " << epoch_number - 1;
        trace_and_halt();
    }

    // Don't connect chains if current epoch doesn't contain a tip yet. See request block persistence for this case
    if (first_request_block.is_zero())
    {
        // Use old request block tip for current epoch
        if (_store.request_tip_put(delegate, epoch_number, prev_e_last, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::LinkAndUpdateTips failed to put request block tip for delegate "
                            << std::to_string(delegate) << " for epoch number " << epoch_number;
            trace_and_halt();
        }
    }
    else
    {
        // Update `next` of last request block in previous epoch
        if (_store.consensus_block_update_next(prev_e_last, first_request_block, ConsensusType::Request, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::LinkAndUpdateTips failed to update prev epoch's "
                            << "request block tip for delegate " << std::to_string(delegate);
            trace_and_halt();
        }

        // Update `previous` of first request block in epoch
        if (_store.request_block_update_prev(first_request_block, prev_e_last, transaction))
        {
            LOG_FATAL(_log) << "PersistenceManager<ECT>::LinkAndUpdateTips failed to update current epoch's "
                            << "first request block prev for delegate " << std::to_string(delegate);
            trace_and_halt();
        }
    }

    // Can safely delete old epoch tip because it's either "rolled over" to current epoch or successfully linked
    _store.request_tip_del(delegate, epoch_number - 1, transaction);
}

bool PersistenceManager<ECT>::BlockExists(
    const ApprovedEB & message)
{
    return _store.epoch_exists(message);
}



void PersistenceManager<ECT>::MarkDelegateElectsAsRemove(MDB_txn* txn)
{
    BlockHash hash;
    assert(!_store.epoch_tip_get(hash,txn));
    ApprovedEB epoch;
    assert(!_store.epoch_get(hash,epoch,txn));

    for(Delegate& d: epoch.delegates)
    {
        if(d.starting_term)
        {
            assert(!_store.candidate_mark_remove(d.account,txn));
        }
    }
}

void PersistenceManager<ECT>::AddReelectionCandidates(MDB_txn* txn)
{
    ApprovedEB epoch;
    assert(!_store.epoch_get_n(3,epoch,txn));

    for(auto& d : epoch.delegates)
    {
        if(d.starting_term)
        {
            RepInfo rep;
            assert(!_store.rep_get(d.account,rep,txn));
            std::shared_ptr<Request> req;
            assert(!_store.request_get(rep.candidacy_action_tip,req,txn));
            if(req->type == RequestType::AnnounceCandidacy)
            {
                auto ac = static_pointer_cast<AnnounceCandidacy>(req); 
                assert(!_store.candidate_add_new(d.account,ac->bls_key,ac->stake,txn));
            }
        }
    }
}

void PersistenceManager<ECT>::UpdateRepresentativesDB(MDB_txn* txn)
{
    for(auto it = logos::store_iterator(txn, _store.representative_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        bool error = false;
        RepInfo info(error,it->second);
        if(!error)
        {
            if(info.remove)
            {
                assert(!mdb_cursor_del(it.cursor,0));
            }
            else if(!info.active)
            {
                info.active = true;
                std::vector<uint8_t> buf;
                assert(!mdb_cursor_put(it.cursor,it->first,info.to_mdb_val(buf),MDB_CURRENT));
            } else
            {
                info.voted = false;
                std::vector<uint8_t> buf;
                assert(!mdb_cursor_put(it.cursor,it->first,info.to_mdb_val(buf),MDB_CURRENT));
            }
        }
    }
}

void PersistenceManager<ECT>::UpdateCandidatesDB(MDB_txn* txn)
{
    std::cout << "updating candidates db" << std::endl;
    for(auto it = logos::store_iterator(txn, _store.candidacy_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        bool error = false;
        CandidateInfo info(error,it->second);
        assert(!error);
        if(info.remove)
        {
            assert(!mdb_cursor_del(it.cursor,0));
        }
        else if(!info.active)
        {
            info.active = true;
            std::vector<uint8_t> buf;

            assert(!mdb_cursor_put(it.cursor,it->first,info.to_mdb_val(buf),MDB_CURRENT));
        }
        else
        {
            info.votes_received_weighted = 0;
            std::vector<uint8_t> buf;
            assert(!mdb_cursor_put(it.cursor,it->first,info.to_mdb_val(buf),MDB_CURRENT));
        }
    }

    _store.clear(_store.leading_candidates_db,txn);
    _store.leading_candidates_size = 0;
}


void PersistenceManager<ECT>::TransitionCandidatesDBNextEpoch(MDB_txn* txn, uint32_t next_epoch_num)
{
    if(next_epoch_num >= EpochVotingManager::START_ELECTIONS_EPOCH)
    {
        AddReelectionCandidates(txn);
    }
    if(next_epoch_num > EpochVotingManager::START_ELECTIONS_EPOCH)
    {
        MarkDelegateElectsAsRemove(txn);
    }
    UpdateCandidatesDB(txn);
}

void PersistenceManager<ECT>::TransitionNextEpoch(MDB_txn* txn, uint32_t next_epoch_num)
{
    TransitionCandidatesDBNextEpoch(txn,next_epoch_num);
    UpdateRepresentativesDB(txn);
}
