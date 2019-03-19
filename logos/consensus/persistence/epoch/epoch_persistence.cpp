/// @file
/// This file contains declaration of Epoch related validation and persistence

#include <logos/consensus/persistence/epoch/epoch_persistence.hpp>
#include <logos/consensus/message_validator.hpp>
#include <logos/epoch/epoch_voting_manager.hpp>
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
        LOG_ERROR(_log) << "PersistenceManager::Validate invalid deligates ";
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
    BlockHash epoch_hash = block.Hash();
    bool transition = !BlockExists(block);

    if(_store.epoch_put(block, transaction) || _store.epoch_tip_put(epoch_hash, transaction))
    {
        LOG_FATAL(_log) << "PersistenceManager::ApplyUpdate failed to store epoch or epoch tip "
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
        LOG_FATAL(_log) << "PersistenceManager::ApplyUpdate failed to get previous block "
                        << block.previous.to_string();
        trace_and_halt();
    }
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
                CandidateInfo candidate(*ac);
                assert(!_store.candidate_put(d.account, candidate, txn));
            }
        }
    }
}

void PersistenceManager<ECT>::UpdateRepresentativesDB(MDB_txn* txn)
{
    for(auto it = logos::store_iterator(txn, _store.remove_reps_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        auto status (mdb_del(txn, _store.representative_db, it->second, nullptr));
        assert(status == 0);
    }

    _store.clear(_store.remove_reps_db, txn);


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

    for(auto it = logos::store_iterator(txn, _store.remove_candidates_db);
            it != logos::store_iterator(nullptr); ++it)
    {
        auto status (mdb_del(txn, _store.candidacy_db, it->second, nullptr));
        assert(status == 0);
    }

    _store.clear(_store.remove_candidates_db, txn);

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
