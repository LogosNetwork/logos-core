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
       //if rep hasnt voted yet this epoch, store fallback voting power
       //to avoid race condition. rep may be voting on epoch boundary 
       if(hash != 0)
       {
           ElectionVote ev;
            if(_store.request_get(hash, ev, txn))
            {
                LOG_FATAL(_log) << "VotingPowerManager::HandleFallback - "
                    "failed to get election vote tip of rep";
                trace_and_halt();
            }

            if(ev.epoch_num < epoch - 1)
            {
                store_fallback = true;
            }            
       }
       else
       {
            store_fallback = true;
       }

      if(store_fallback)
      {
          VotingPowerFallback f;
          f.power = GetPower(info);
          f.total_stake = info.current.locked_proxied + info.current.self_stake;
          _store.put(_store.voting_power_fallback_db,rep,f,txn);
      } 
      else
      {
          //if voted, delete previous fallback record, if one exists
        _store.del(_store.voting_power_fallback_db,rep,txn);
      }
    }
    else
    {
        //delete previous fallback record, if one exists
        _store.del(_store.voting_power_fallback_db,rep,txn);
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
    else
    {
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

}


void VotingPowerManager::StoreOrPrune(
        AccountAddress const & rep,
        VotingPowerInfo& info,
        MDB_txn* txn)
{
    if(CanPrune(rep,info,txn))
    {
        _store.del(_store.voting_power_db,rep,txn);
    }
    else
    {
        _store.put(_store.voting_power_db,rep,info,txn);
    }
}

bool VotingPowerManager::CanPrune(
        AccountAddress const & rep,
        VotingPowerInfo const & info,
        MDB_txn* txn)
{

    //need to check next instead of current, because when next goes to 0,
    //the record may never be updated again
    Amount total_power = info.next.locked_proxied
        + info.next.unlocked_proxied
        + info.next.self_stake;
    bool power_is_zero = (
            info.next.locked_proxied 
            + info.next.unlocked_proxied 
            + info.next.self_stake) == 0;
    if(!power_is_zero)
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
    bool found = !_store.get(_store.voting_power_db,rep,info,txn);
    if(found && CanPrune(rep, info, txn))
    {
        LOG_INFO(_log) << "pruning rep = " << rep.to_string()
            << " info = " << info.next.self_stake.to_string();
        _store.del(_store.voting_power_db,rep,txn);
    }
}




bool VotingPowerManager::SubtractLockedProxied(
        AccountAddress const & rep,
        Amount const & amount,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::SubtractLockedProxied - "
            << "txn is null";
        trace_and_halt();
    }

    VotingPowerInfo info;
    if(_store.get(_store.voting_power_db,rep,info,txn))
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
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::AddLockedProxied - "
            << "txn is null";
        trace_and_halt();
    }

    VotingPowerInfo info;
    if(_store.get(_store.voting_power_db,rep,info,txn))
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
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::SubtractUnlockedProxied - "
            << "txn is null";
        trace_and_halt();
    }

    VotingPowerInfo info;
    if(_store.get(_store.voting_power_db,rep,info,txn))
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
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::AddUnlockedProxied - txn is null";
        trace_and_halt();
    }
    VotingPowerInfo info;

    if(_store.get(_store.voting_power_db,rep,info,txn))
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

    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::SubtractSelfStake - txn is null";
        trace_and_halt();
    }
    VotingPowerInfo info;

    if(_store.get(_store.voting_power_db,rep,info,txn))
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
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::AddSelfStake - txn is null";
        trace_and_halt();
    }
    VotingPowerInfo info;

    if(_store.get(_store.voting_power_db,rep,info,txn))
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
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::GetCurrentVotingPower - "
            << "txn is null";
        trace_and_halt();
    }
    VotingPowerInfo info;
    if(_store.get(_store.voting_power_db,rep,info,txn))
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
        if(_store.get(_store.voting_power_fallback_db, rep, f, txn))
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
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::GetCurrentVotingPower - "
            << "txn is null";
        trace_and_halt();
    }
    VotingPowerInfo info;
    if(_store.get(_store.voting_power_db,rep,info,txn))
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
        if(_store.get(_store.voting_power_fallback_db, rep, f, txn))
        {
        
        LOG_FATAL(_log) << "VotingPowerManager::GetCurrentVotingPower - "
            << "failed to get fallback record";
        trace_and_halt();
        }
        
        return f.power;
    }

    return GetPower(info);
}


//Note, this function does not transition, only use for testing
bool VotingPowerManager::GetVotingPowerInfo(AccountAddress const & rep, VotingPowerInfo& info, MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::GetVotingPowerInfo - "
            << "txn is null";
        trace_and_halt();
    }
    return !_store.get(_store.voting_power_db,rep,info,txn);
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



