#include <logos/staking/staking_manager.hpp>

StakedFunds StakingManager::CreateStakedFunds(
        AccountAddress const & target,
        AccountAddress const & source,
        MDB_txn* txn)
{
    StakedFunds funds;
    funds.amount = 0;
    funds.target = target;
    funds.liability_hash = 
        _liability_mgr.CreateUnexpiringLiability(target, source, 0, txn); 
    return funds;
}

ThawingFunds StakingManager::CreateThawingFunds(
        AccountAddress const & target,
        AccountAddress const & source,
        uint32_t const & epoch_created,
        MDB_txn* txn)
{
    ThawingFunds funds;
    funds.amount = 0;
    funds.target = target;
    funds.expiration_epoch = epoch_created + 42;
    funds.liability_hash = 
        _liability_mgr.CreateExpiringLiability(target, source, 0, funds.expiration_epoch, txn);
    return funds;
}


void StakingManager::UpdateAmount(
        ThawingFunds & funds,
        AccountAddress const & origin,
        Amount const & amount,
        MDB_txn* txn)
{
    //Since thawing_db uses duplicate keys, must delete old record
    //and store new record in order to update
    Delete(funds, origin, txn);
    funds.amount = amount;

    if(funds.amount > 0)
    {
        Store(funds, origin, txn);
    }
    else
    {
        _liability_mgr.DeleteLiability(funds.liability_hash, txn);
    }
}

void StakingManager::UpdateAmount(
        StakedFunds & funds,
        AccountAddress const & origin,
        Amount const & amount,
        MDB_txn* txn)
{
    funds.amount = amount;

    if(funds.amount > 0)
    {
        Store(funds, origin, txn);
    }
    else
    {
        Delete(funds, origin, txn);
        _liability_mgr.DeleteLiability(funds.liability_hash,txn);
    }
}

uint32_t GetExpiration(ThawingFunds const & funds)
{
    return funds.expiration_epoch;
}

uint32_t GetExpiration(StakedFunds const & funds)
{
    return 0;
}


template <typename T, typename R>
Amount StakingManager::Extract(
        T & input,
        R & output,
        Amount const & amount,
        AccountAddress const & origin,
        uint32_t const & epoch,
        MDB_txn* txn)
{
    Amount to_extract = amount;
    if(to_extract > input.amount)
    {
        to_extract = input.amount;
    }
    if(input.target != output.target)
    {
        if(input.target == origin)
        {
            return 0;
        }
        uint32_t liability_expiration = GetExpiration(input);
        if(liability_expiration  == 0)
        {
            liability_expiration = epoch + 42;
        }
        bool res = _liability_mgr.CreateSecondaryLiability(input.target,origin,to_extract,liability_expiration, txn);
        if(!res)
        {
            return 0;
        }
    }
    UpdateAmount(input, origin, input.amount - to_extract, txn);
    output.amount += to_extract;
    
    return to_extract;
}


//TODO: reading out the available balance here may be ignoring/overwriting pending
//updates to account balance
void StakingManager::StakeAvailableFunds(
        StakedFunds & output,
        Amount const & amount,
        AccountAddress const & origin,
        uint32_t const & epoch,
        MDB_txn* txn)
{
    logos::account_info info;
    if(_store.account_get(origin, info, txn))
    {
        LOG_FATAL(_log) << "StakingManager::StakeAvailableFunds - "
            << "account info not found. account = " << origin.to_string();
        trace_and_halt();
    }
    if(amount > info.GetAvailableBalance())
    {
        LOG_FATAL(_log) << "StakingManager::StakeAvailableFunds - "
            << "not enough available balance. account = " << origin.to_string();
        trace_and_halt();
    }
    info.SetAvailableBalance(info.GetAvailableBalance() - amount, epoch, txn);
    _store.account_put(origin, info, txn);
    output.amount += amount;
}

void StakingManager::Store(
        StakedFunds const & funds,
        AccountAddress const & origin,
        MDB_txn* txn)
{
    _store.put(_store.staking_db, logos::mdb_val(origin), funds, txn);
    _liability_mgr.UpdateLiabilityAmount(funds.liability_hash, funds.amount, txn);
}

void StakingManager::Store(
        ThawingFunds & funds,
        AccountAddress const & origin,
        MDB_txn* txn)
{
    for(auto it = logos::store_iterator(txn,_store.thawing_db, logos::mdb_val(origin));
            it != logos::store_iterator(nullptr) && it->first.uint256() == origin; ++it)
    {
        ThawingFunds t; 
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (it->second.data ()), it->second.size ());
        bool error = t.Deserialize (stream);
        if(error)
        {
            LOG_FATAL(_log) << "StakingManager::IterateThawingFunds - "
                << "Error deserializing ThawingFunds for account = " << origin.to_string();
            trace_and_halt();
        }
        if(t.target == funds.target && t.expiration_epoch == funds.expiration_epoch)
        {
            std::vector<uint8_t> buf;
            funds.amount += t.amount;
            mdb_cursor_put(it.cursor,it->first,funds.to_mdb_val(buf),MDB_CURRENT);
            return;
        }

    }
    _store.put(_store.thawing_db, logos::mdb_val(origin), funds, txn);
    _liability_mgr.UpdateLiabilityAmount(funds.liability_hash, funds.amount, txn);
}

void StakingManager::Delete(
        ThawingFunds const & funds,
        AccountAddress const & origin,
        MDB_txn* txn)
{
    std::vector<uint8_t> buf;
    //thawing_db uses duplicate keys. Need to pass in value to delete
    mdb_del(txn, _store.thawing_db, logos::mdb_val(origin), funds.to_mdb_val(buf));
}

void StakingManager::Delete(
        StakedFunds const & funds,
        AccountAddress const & origin,
        MDB_txn* txn)
{
    _store.del(_store.staking_db, logos::mdb_val(origin), txn);
}

StakedFunds StakingManager::GetCurrentStakedFunds(
        AccountAddress const & origin,
        MDB_txn* txn)
{
    StakedFunds funds;
    _store.get(_store.staking_db, logos::mdb_val(origin), funds, txn);
    return funds;
}

std::vector<ThawingFunds> StakingManager::GetThawingFunds(
        AccountAddress const & origin,
        MDB_txn* txn)
{
    std::vector<ThawingFunds> thawing;
    auto make_vec = [&](ThawingFunds & funds)
    {
        thawing.push_back(funds);
        return true;
    };
    IterateThawingFunds(origin, make_vec, txn);
    return thawing;
}

void StakingManager::IterateThawingFunds(
        AccountAddress const & origin,
        std::function<bool(ThawingFunds & funds)> func,
        MDB_txn* txn)
{
    for(auto it = logos::store_iterator(txn,_store.thawing_db, logos::mdb_val(origin));
            it != logos::store_iterator(nullptr); ++it)
    {
        if(it->first.uint256() != origin)
        {
            return;
        }
        ThawingFunds t; 
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (it->second.data ()), it->second.size ());
        bool error = t.Deserialize (stream);
        if(error)
        {
            LOG_FATAL(_log) << "StakingManager::IterateThawingFunds - "
                << "Error deserializing ThawingFunds for account = " << origin.to_string();
            trace_and_halt();
        }
        if(!func(t))
        {
            return;
        }
    }

}

//TODO staking_subchain head needs to be up to date before this function is 
//called, for updates to available balance to work correctly
void StakingManager::Stake(AccountAddress const & origin,
        Amount const & amount,
        AccountAddress const & target,
        uint32_t const & epoch,
        MDB_txn* txn)
{
    Amount amount_left = amount;
    StakedFunds cur_stake(GetCurrentStakedFunds(origin, txn));
    //no current stake
    if(cur_stake.amount == 0)
    {
        cur_stake = CreateStakedFunds(target, origin, txn);
    }

    auto begin_thawing = [&](Amount const & amount_to_thaw)
    {
        ThawingFunds thawing = CreateThawingFunds(cur_stake.target, origin, epoch, txn);
        Extract(cur_stake, thawing, amount_to_thaw, origin, epoch, txn);
        Store(thawing, origin, txn);

    };
    //if changing target, extract from existing stake
    if(target != cur_stake.target)
    {
        if(cur_stake.target == origin)
        {
            _voting_power_mgr.SubtractSelfStake(cur_stake.target, cur_stake.amount, epoch, txn);
        }
        else
        {
            _voting_power_mgr.SubtractLockedProxied(cur_stake.target, cur_stake.amount, epoch, txn);
            logos::account_info info;
            _store.account_get(origin, info, txn);
            _voting_power_mgr.SubtractUnlockedProxied(cur_stake.target,info.GetAvailableBalance(),epoch,txn);
            if(target != origin)
            {
                _voting_power_mgr.AddUnlockedProxied(target,info.GetAvailableBalance(), epoch, txn);
            }
        }
        StakedFunds new_stake = CreateStakedFunds(target, origin, txn);
        amount_left -= Extract(cur_stake, new_stake, amount_left, origin, epoch, txn);
        if(cur_stake.amount > 0)
        {
            begin_thawing(cur_stake.amount);
        }
        _voting_power_mgr.AddLockedProxied(new_stake.target, new_stake.amount, epoch, txn);
        cur_stake = new_stake;
    }
    else if(amount_left < cur_stake.amount)
    {
        Amount amount_to_thaw = cur_stake.amount - amount_left;
        if(cur_stake.target == origin)
        {
           _voting_power_mgr.SubtractSelfStake(cur_stake.target, amount_to_thaw, epoch, txn); 
        }
        else
        {
            _voting_power_mgr.SubtractLockedProxied(cur_stake.target, amount_to_thaw, epoch, txn);
        }
        begin_thawing(amount_to_thaw);
        return;
    }
    else
    {
        amount_left -= cur_stake.amount;
    }
    if(amount_left > 0)
    {
       if(target == origin)
       {
        _voting_power_mgr.AddSelfStake(target, amount_left, epoch, txn);
       }
       else
       {
        _voting_power_mgr.AddLockedProxied(target, amount_left, epoch, txn);
       }
       auto extract_thawing = [&](ThawingFunds & t)
       {
            amount_left -= Extract(t, cur_stake, amount_left, origin, epoch, txn);
            return amount_left > 0;
       }; 
       IterateThawingFunds(origin, extract_thawing, txn);
       if(amount_left > 0)
       {
            StakeAvailableFunds(cur_stake, amount_left, origin, epoch, txn);
       }
    }
    Store(cur_stake, origin, txn);
}

bool StakingManager::Validate(
        AccountAddress const & origin,
        Amount const & amount,
        AccountAddress const & target,
        uint32_t const & epoch,
        MDB_txn* txn)
{
    logos::account_info info;
    if(_store.account_get(origin, info, txn))
    {
        LOG_FATAL(_log) << "StakingManager::Validate - "
            << "failed to get account info for account = " << origin.to_string();
        trace_and_halt();
    }
    Amount available = info.GetAvailableBalance();
    if(available > amount)
    {
        return true;
    }
    Amount remaining = amount - available;
    StakedFunds cur_stake(GetCurrentStakedFunds(origin, txn));
    bool secondary_liability_created = false;
    if(cur_stake.amount > 0)
    {
        if(cur_stake.target == target 
                || _liability_mgr.CanCreateSecondaryLiability(target, origin, txn))
        {
            if(cur_stake.amount > remaining)
            {
                return true;
            }
            remaining -= cur_stake.amount;
            secondary_liability_created = cur_stake.target != target;
        }
    }
    bool res = false;
    IterateThawingFunds(origin,[&](ThawingFunds & t)
            {
                if(t.target == target 
                        || (!secondary_liability_created &&
                           _liability_mgr.CanCreateSecondaryLiability(target, origin, txn))
                        || (secondary_liability_created && cur_stake.target == target))
                {
                    if(t.amount > remaining)
                    {
                        res = true;
                        return false;   
                    }
                    remaining -= t.amount;
                } 
            },txn);
    return res;
}


void StakingManager::PruneThawing(
        AccountAddress const & origin,
        uint32_t const & cur_epoch,
        MDB_txn* txn)
{

    for(auto it = logos::store_iterator(txn,_store.thawing_db, logos::mdb_val(origin));
            it != logos::store_iterator(nullptr); ++it)
    {
        if(it->first.uint256() != origin)
        {
            return;
        }
        ThawingFunds t; 
        logos::bufferstream stream (reinterpret_cast<uint8_t const *> (it->second.data ()), it->second.size ());
        bool error = t.Deserialize (stream);
        if(error)
        {
            LOG_FATAL(_log) << "StakingManager::IterateThawingFunds - "
                << "Error deserializing ThawingFunds for account = " << origin.to_string();
            trace_and_halt();
        }
        if(t.expiration_epoch <= cur_epoch)
        {
           if(mdb_cursor_del(it.cursor,0))
           {
                LOG_FATAL(_log) << "StakingManager::PruneThawing - "
                    << "Error deleting ThawingFunds. orign = " << origin.to_string();
                trace_and_halt();
           }
        }
        else
        {
            //thawing funds are ordered by expiration
            //as soon as we see an unexpired thawing fund, we know
            //later funds will also be unexpired
            return;
        }
        
    }
    
}
