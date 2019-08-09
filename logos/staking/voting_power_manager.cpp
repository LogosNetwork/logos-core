#include <logos/staking/voting_power_manager.hpp>
#include <logos/common.hpp>

std::shared_ptr<VotingPowerManager> VotingPowerManager::instance = 0;

Amount GetPower(VotingPowerInfo const & info)
{
    Amount diluted_unlocked_proxied = 
        (info.current.unlocked_proxied.number() * DILUTION_FACTOR) / 100;
    return info.current.self_stake 
        + info.current.locked_proxied 
        + diluted_unlocked_proxied; 
}

//There is a certain race condition regarding voting near the epoch boundary
//Problem:
//Consider an ElectionVote submitted at the very end of epoch i,
//and a Send submitted at the very beginning of epoch i+1
//Assume the origin account of the Send has a rep. Assume this rep is
//the origin account of the ElectionVote
//When the ElectionVote is applied, the software will look up the reps voting
//power for epoch i in voting_power_db
//When the Send is applied, the software will transition reps voting power
//to epoch i+1. During this transition, the voting power for epoch i is overwritten
//If the Send is applied before the ElectionVote, the voting power for the rep
//for epoch i is no longer stored anywhere. 
//Solution:
//To mitigate this race condition,there is a special database called voting_power_fallback_db
//Whenever the software is transitioning voting power of a rep to epoch i+1, the software
//checks that the rep voted in epoch i. If the rep did not vote in epoch i, the reps
//voting power for epoch i is first stored in voting_power_fallback_db, and then voting power
//for the rep (stored in voting_power_db) is transitioned to epoch i + 1.
//If the rep did vote in epoch i, no data is stored in voting_power_fallback_db.
//When applying an ElectionVote, the software checks if the epoch number of the
//ElectionVote is less than the epoch modified field of VotingPowerInfo. If so,
//the software reads voting power from voting_power_fallback_db. Otherwise, the
//software reads voting power from voting_power_db
void VotingPowerManager::HandleFallback(
        VotingPowerInfo const & info,
        AccountAddress const & rep,
        uint32_t epoch,
        MDB_txn* txn)
{
    RepInfo rep_info;
    if(!_store.rep_get(rep, rep_info, txn))
    {
       auto hash = rep_info.election_vote_tip;
       bool store_fallback = false;
       if(hash != 0)
       {
           ElectionVote ev;
           if(_store.request_get(hash, ev, txn))
           {
               LOG_FATAL(_log) << "VotingPowerManager::HandleFallback - "
                   "failed to get election vote tip of rep";
               trace_and_halt();
           }

           //if rep did not vote in previous epoch, store fallback voting power
           //to avoid race condition. rep may be voting on epoch boundary 
           if(ev.epoch_num < epoch - 1)
           {
               store_fallback = true;
           }            
       }
       //rep has never voted, store fallback
       else
       {
            store_fallback = true;
       }

      if(store_fallback)
      {
          VotingPowerFallback f;
          f.power = GetPower(info);
          f.total_stake = info.current.locked_proxied + info.current.self_stake;
          _store.fallback_voting_power_put(rep,f,txn);
      } 
      else
      {
          //if voted, delete previous fallback record, if one exists
        _store.fallback_voting_power_del(rep,txn);
      }
    }
    else
    {
        //delete previous fallback record, if one exists
        _store.fallback_voting_power_del(rep,txn);
    }
}

bool VotingPowerManager::TransitionIfNecessary(
        VotingPowerInfo& info,
        uint32_t const & epoch,
        AccountAddress const & rep,
        MDB_txn* txn)
{
    if(epoch > info.epoch_modified)
    {
        HandleFallback(info,rep,epoch,txn);
        info.current = info.next;
        info.epoch_modified = epoch;
        return true;
    }
    return false;
}


void VotingPowerManager::Modify(
        VotingPowerInfo& info,
        AccountAddress const & account,
        STAKE_TYPE stake_type,
        OP_TYPE op_type,
        uint32_t const & epoch,
        Amount const & diff,
        MDB_txn* txn)
{
    TransitionIfNecessary(info, epoch, account, txn);
    auto func = [&op_type](Amount& data, Amount const& diff)
    {
        if(diff == 0) return;
        if(op_type == ADD)
        {
            data += diff;
            if(data < diff)
            {
                Log log;
                LOG_FATAL(log) << "VotingPowerManager::AddFunc - "
                    << " overflow - data = " << data.to_string()
                    << " diff = " << diff.to_string();
                trace_and_halt();
            }
        }
        else
        {
            if(diff > data)
            {
                Log log;
                LOG_FATAL(log) << "VotingPowerManager::SubtractFunc - "
                    << " overflow - data = " << data.to_string()
                    << " diff = " << diff.to_string();
                trace_and_halt();
            }

            data -= diff;
        }
    };


    if(epoch < info.epoch_modified)
    {
        if(stake_type == LOCKED_PROXY)
        {
            func(info.current.locked_proxied, diff);
        }
        else if(stake_type == UNLOCKED_PROXY)
        {
            func(info.current.unlocked_proxied, diff);
        }
        else
        {
            func(info.current.self_stake, diff);
        }
    }
    //Updates from the previous epoch effect current as well as next
    if(stake_type == LOCKED_PROXY)
    {
        func(info.next.locked_proxied, diff);
    }
    else if(stake_type == UNLOCKED_PROXY)
    {
        func(info.next.unlocked_proxied, diff);
    }
    else
    {
        func(info.next.self_stake, diff);
    }

}


void VotingPowerManager::StoreOrPrune(
        AccountAddress const & rep,
        VotingPowerInfo& info,
        MDB_txn* txn)
{
    if(CanPrune(rep,info,txn))
    {
        if(_store.voting_power_del(rep,txn))
        {
            LOG_FATAL(_log) << "VotingPowerManager::StoreOrPrune - "
                << "error pruning rep = "
                << rep.to_string();
            trace_and_halt();
        }
    }
    else
    {
        _store.voting_power_put(rep,info,txn);
    }
}

bool VotingPowerManager::CanPrune(
        AccountAddress const & rep,
        VotingPowerInfo const & info,
        MDB_txn* txn)
{

    //need to check next instead of current
    if(info.next.locked_proxied != 0 || info.next.self_stake != 0 || info.next.unlocked_proxied != 0)
    {
        return false;
    }
    //if account is still rep, don't delete. EpochVotingManager will delete
    RepInfo rep_info; 
    bool is_rep = !_store.rep_get(rep,rep_info,txn);
    return !is_rep;

}

void VotingPowerManager::TryPrune(
        AccountAddress const & rep,
        MDB_txn* txn)
{
    VotingPowerInfo info;
    bool found = GetVotingPowerInfo(rep,info,txn);
    if(found && CanPrune(rep, info, txn))
    {
       if(_store.voting_power_del(rep,txn))
       {
            LOG_FATAL(_log) << "VotingPowerManager::TryPrune - "
                << "error pruning rep = "
                << rep.to_string();
            trace_and_halt();
       }
    }
}




bool VotingPowerManager::SubtractLockedProxied(
        AccountAddress const & rep,
        Amount const & amount,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    VotingPowerInfo info;
    if(!GetVotingPowerInfo(rep,info,txn))
    {
        LOG_FATAL(_log) << "VotingPowerManager::SubtractLockedProxied - "
            << "VotingPowerInfo does not exist for rep = " << rep.to_string();
        trace_and_halt();
    }

    Modify(info,
           rep,
           STAKE_TYPE::LOCKED_PROXY,
           OP_TYPE::SUBTRACT,
           epoch_number,
           amount,
           txn);

    StoreOrPrune(rep,info,txn);

    return false;
}

bool VotingPowerManager::AddLockedProxied(
        AccountAddress const & rep,
        Amount const & amount,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    VotingPowerInfo info;
    if(!GetVotingPowerInfo(rep,info,txn))
    {
        LOG_FATAL(_log) << "VotingPowerManager::AddLockedProxied - "
            << "VotingPowerInfo does not exist for rep = " << rep.to_string();
        trace_and_halt();
    }

    Modify(info,
           rep,
           STAKE_TYPE::LOCKED_PROXY,
           OP_TYPE::ADD,
           epoch_number,
           amount,
           txn);

    StoreOrPrune(rep,info,txn);

    return false;
}

bool VotingPowerManager::SubtractUnlockedProxied(
        AccountAddress const & rep,
        Amount const & amount,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    VotingPowerInfo info;
    if(!GetVotingPowerInfo(rep,info,txn))
    {
        LOG_FATAL(_log) << "VotingPowerManager::SubtractUnlockedProxied - "
            << "VotingPowerInfo does not exist for rep = " << rep.to_string();
        trace_and_halt();
    }

    Modify(info,
           rep,
           STAKE_TYPE::UNLOCKED_PROXY,
           OP_TYPE::SUBTRACT,
           epoch_number,
           amount,
           txn);

    StoreOrPrune(rep,info,txn);

    return false;
}


bool VotingPowerManager::AddUnlockedProxied(
        AccountAddress const & rep,
        Amount const & amount,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    VotingPowerInfo info;
    if(!GetVotingPowerInfo(rep,info,txn))
    {
        LOG_FATAL(_log) << "VotingPowerManager::AddUnlockedProxied - "
            << "VotingPowerInfo does not exist for rep = " << rep.to_string();
        trace_and_halt();
    }

    Modify(info,
           rep,
           STAKE_TYPE::UNLOCKED_PROXY,
           OP_TYPE::ADD,
           epoch_number,
           amount,
           txn);

    StoreOrPrune(rep,info,txn);

    return false;
}



bool VotingPowerManager::SubtractSelfStake(
        AccountAddress const & rep,
        Amount const & amount,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{

    VotingPowerInfo info;
    if(!GetVotingPowerInfo(rep,info,txn))
    {
        LOG_FATAL(_log) << "VotingPowerManager::SubtractSelfStake - "
            << "VotingPowerInfo does not exist for rep = " << rep.to_string();
        trace_and_halt();
    }

    Modify(info,
           rep,
           STAKE_TYPE::SELF_STAKE,
           OP_TYPE::SUBTRACT,
           epoch_number,
           amount,
           txn);

    StoreOrPrune(rep,info,txn);

    return false;
}



bool VotingPowerManager::AddSelfStake(
        AccountAddress const & rep,
        Amount const & amount,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    VotingPowerInfo info;
    if(!GetVotingPowerInfo(rep,info,txn))
    {
        LOG_WARN(_log) << "VotingPowerManager::AddSelfStake - "
            << "VotingPowerInfo does not exist for rep = " << rep.to_string()
            << " . Creating new VotingPowerInfo";
        info.epoch_modified = epoch_number;
    }

    Modify(info,
           rep,
           STAKE_TYPE::SELF_STAKE,
           OP_TYPE::ADD,
           epoch_number,
           amount,
           txn);

    StoreOrPrune(rep,info,txn);

    return false;
}

Amount VotingPowerManager::GetCurrentTotalStake(
        AccountAddress const & rep,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    VotingPowerInfo info;
    if(!GetVotingPowerInfo(rep,info,txn))
    {
        LOG_FATAL(_log) << "VotingPowerManager::GetCurrentVotingPower - "
            << "VotingPowerInfo does not exist for rep = " << rep.to_string();
        trace_and_halt();
    }
    
    if(TransitionIfNecessary(info,epoch_number,rep,txn))
    {
        StoreOrPrune(rep, info, txn);
    }
    if(epoch_number < info.epoch_modified)
    {
        VotingPowerFallback f;
        if(_store.fallback_voting_power_get(rep, f, txn))
        {

            LOG_FATAL(_log) << "VotingPowerManager::GetCurrentVotingPower - "
                << "failed to get fallback record";
            trace_and_halt();
        }
        
        return f.total_stake;
    }

    return info.current.locked_proxied + info.current.self_stake;
}


Amount VotingPowerManager::GetCurrentVotingPower(
        AccountAddress const & rep,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{    
    VotingPowerInfo info;
    if(!GetVotingPowerInfo(rep,info,txn))
    {
        LOG_FATAL(_log) << "VotingPowerManager::GetCurrentVotingPower - "
            << "VotingPowerInfo does not exist for rep = " << rep.to_string();
        trace_and_halt();
    }
    
    if(TransitionIfNecessary(info,epoch_number,rep,txn))
    {
        StoreOrPrune(rep, info, txn);
    }
    if(epoch_number < info.epoch_modified)
    {
        VotingPowerFallback f;
        if(_store.fallback_voting_power_get(rep, f, txn))
        {
            LOG_FATAL(_log) << "VotingPowerManager::GetCurrentVotingPower - "
                << "failed to get fallback record";
            trace_and_halt();
        }
        
        return f.power;
    }

    return GetPower(info);
}


//Note, this function does not transition, only use for testing or internally
bool VotingPowerManager::GetVotingPowerInfo(AccountAddress const & rep, VotingPowerInfo& info, MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::GetVotingPowerInfo - "
            << "txn is null";
        trace_and_halt();
    }
    return !_store.voting_power_get(rep,info,txn);
}

bool VotingPowerManager::GetVotingPowerInfo(
        AccountAddress const & rep,
        uint32_t const & epoch,
        VotingPowerInfo& info,
        MDB_txn* txn)
{
    if(GetVotingPowerInfo(rep, info, txn))
    {
        if(TransitionIfNecessary(info,epoch,rep,txn))
        {
            StoreOrPrune(rep, info, txn);
        }
        return true;
    }
    return false;
}



