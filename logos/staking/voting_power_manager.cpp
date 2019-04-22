#include <logos/staking/voting_power_manager.hpp>
#include <logos/common.hpp>

std::shared_ptr<VotingPowerManager> VotingPowerManager::instance = 0;

void TransitionIfNecessary(
        VotingPowerInfo& info,
        uint32_t const & epoch)
{
    if(epoch> info.epoch_modified)
    {
        info.current = info.next;
        info.epoch_modified = epoch;
    }
}

enum class DiffType
{
ADD = 1,
SUBTRACT = 2
};

void Modify(
        VotingPowerInfo& info,
        Amount VotingPowerSnapshot::*snapshot_member,
        uint32_t const & epoch,
        Amount const & diff,
        DiffType diff_type)
{
    TransitionIfNecessary(info, epoch);

    VotingPowerSnapshot VotingPowerInfo::*info_member
        = epoch < info.epoch_modified 
            ? &VotingPowerInfo::current : &VotingPowerInfo::next;

    if(diff_type == DiffType::ADD)
    {
        info.*info_member.*snapshot_member += diff;
    }
    else
    {
        info.*info_member.*snapshot_member -= diff;
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
           &VotingPowerSnapshot::locked_proxied,
           epoch_number,
           amount,
           DiffType::SUBTRACT);

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
           &VotingPowerSnapshot::locked_proxied,
           epoch_number,
           amount,
           DiffType::ADD);

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
           &VotingPowerSnapshot::unlocked_proxied,
           epoch_number,
           amount,
           DiffType::SUBTRACT);

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
           &VotingPowerSnapshot::unlocked_proxied,
           epoch_number,
           amount,
           DiffType::ADD);

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
           &VotingPowerSnapshot::self_stake,
           epoch_number,
           amount,
           DiffType::SUBTRACT);

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
    }

    Modify(info,
           &VotingPowerSnapshot::self_stake,
           epoch_number,
           amount,
           DiffType::ADD);

    StoreOrPrune(rep,info,txn);

    return false;
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
    
    TransitionIfNecessary(info,epoch_number);

    //TODO: dilution factor
    Amount diluted_unlocked_proxied = 
        (info.current.unlocked_proxied.number() * DILUTION_FACTOR) / 100;
    return info.current.self_stake 
        + info.current.locked_proxied 
        + diluted_unlocked_proxied; 
}


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


AccountAddress VotingPowerManager::GetRep(
        logos::account_info const & info,
        MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "VotingPowerManager::GetRep - txn is null";
        trace_and_halt();    
    }
    //TODO return actual rep
    std::shared_ptr<Request> req;
    if(info.staking_subchain_head == 0)
    {
        LOG_WARN(_log) << "VotingPowerManager::GetRep - account has no rep";
    }
    else
    {
        if(_store.request_get(info.staking_subchain_head, req, txn))
        {
            LOG_FATAL(_log) << "VotingPowerManager::GetRep - "
                << "Error getting staking subchain head";
            trace_and_halt();
        }
        if(req->type == RequestType::Proxy)
        {
            return static_pointer_cast<Proxy>(req)->rep;
        }
        else
        {
            //TODO handle other request types
            LOG_FATAL(_log) << "VotingPowerManager::GetRep - "
                << "Request on staking subchain is not Proxy";
            trace_and_halt();
        }
    }
    return 0;
}
















