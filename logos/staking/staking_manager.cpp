#include <logos/staking/staking_manager.hpp>

std::shared_ptr<StakingManager> StakingManager::instance = 0;

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
    funds.expiration_epoch = epoch_created + THAWING_PERIOD;
    funds.liability_hash = 
        _liability_mgr.CreateExpiringLiability(target, source, 0, funds.expiration_epoch, txn);
    return funds;
}


void StakingManager::UpdateAmountAndStore(
        ThawingFunds & funds,
        AccountAddress const & origin,
        Amount const & new_amount,
        MDB_txn* txn)
{
    //Since thawing_db uses duplicate keys, must delete old record
    //and store new record in order to update
    Delete(funds, origin, txn);
    funds.amount = new_amount;

    if(funds.amount > 0)
    {
        Store(funds, origin, txn);
    }
    else
    {
        _liability_mgr.DeleteLiability(funds.liability_hash, txn);
    }
}

void StakingManager::UpdateAmountAndStore(
        StakedFunds & funds,
        AccountAddress const & origin,
        Amount const & new_amount,
        MDB_txn* txn)
{
    funds.amount = new_amount;

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
        Amount amount_to_extract,
        AccountAddress const & origin,
        uint32_t const & epoch,
        MDB_txn* txn)
{
    if(amount_to_extract > input.amount)
    {
        //if the argument passed in is greater than max possible to extract
        //just extract max possible (all of input)
        amount_to_extract = input.amount;
    }
    //changing target is a special case
    //need to handle secondary liabilities
    if(input.target != output.target)
    {
        //Can't extract self stake into lock proxy
        if(input.target == origin)
        {
            //no funds were extracted, return 0
            return 0;
        }
        //If extracting from thawing, secondary liability will have same
        //expiration as ThawingFunds
        uint32_t liability_expiration = GetExpiration(input);
        if(liability_expiration  == 0)
        {
            //if liability_expiration is 0, input is StakedFunds
            //therefore, need to set expiration of liability to one thawing
            //period past current epoch
            liability_expiration = epoch + THAWING_PERIOD;
        }

        //Whenever extracting into diff target, need to create secondary
        //liability
        bool res = _liability_mgr.CreateSecondaryLiability(input.target,origin,amount_to_extract,liability_expiration,txn);
        //failed to create secondary liability
        //this can happen if origin already has secondary liabilities with a
        //different target
        if(!res)
        {
            //no funds were extracted, return 0
            return 0;
        }
    }
    //Adjust the amount of input based on how much was extracted
    //Note, UpdateAmountAndStore() updates associated liabilities
    //and handles deletion if amount == 0
    //if amount_to_extract == input.amount, input will be deleted from db
    //as well as associated liabilities
    UpdateAmountAndStore(input, origin, input.amount - amount_to_extract, txn);

    //Defer storing of output to caller, since caller may
    //may call Extract multiple times with same output and different input
    //See Stake() member function
    output.amount += amount_to_extract;
    
    LOG_DEBUG(_log) << "amount_to_extract = " << amount_to_extract.to_string();
    //Return the amount extracted, which could be less than amount_to_extract
    //see first if statement of this function
    return amount_to_extract;
}


void StakingManager::StakeAvailableFunds(
        StakedFunds & output,
        Amount const & amount,
        AccountAddress const & origin,
        logos::account_info & account_info,
        uint32_t const & epoch,
        MDB_txn* txn)
{
    if(amount > account_info.GetAvailableBalance())
    {
        LOG_FATAL(_log) << "StakingManager::StakeAvailableFunds - "
            << "not enough available balance. account = " << origin.to_string();
        trace_and_halt();
    }
    account_info.SetAvailableBalance(account_info.GetAvailableBalance() - amount, epoch, txn);
    output.amount += amount;
}

void StakingManager::Store(
        StakedFunds const & funds,
        AccountAddress const & origin,
        MDB_txn* txn)
{
    _store.stake_put(origin, funds, txn);
    _liability_mgr.UpdateLiabilityAmount(funds.liability_hash, funds.amount, txn);
}

bool StakingManager::Store(
        ThawingFunds & funds,
        AccountAddress const & origin,
        MDB_txn* txn)
{
    bool consolidated = false;
    auto update = [&funds,&consolidated,&txn,this](ThawingFunds& t, logos::store_iterator& it)
    {
        //Thawing funds with same target and expiration epoch are consolidated
        if(t.target == funds.target && t.expiration_epoch == funds.expiration_epoch)
        {
            assert(t.liability_hash == funds.liability_hash);
            std::vector<uint8_t> buf;
            t.amount += funds.amount;
            mdb_cursor_put(it.cursor,it->first,t.to_mdb_val(buf),MDB_CURRENT);
            _liability_mgr.UpdateLiabilityAmount(t.liability_hash,t.amount,txn);
            consolidated = true;
            return false;
        }
        //return t.expiration_epoch > funds.expiration_epoch;
        else if(t.expiration_epoch < funds.expiration_epoch)
        {
            //Thawing funds are stored in reverse order of expiration
            //expiration_epoch will only continue to decrease
            return false;
        }
        return true;
    };
    ProcessThawingFunds(origin,update,txn);

    if(!consolidated)
    {
        _store.thawing_put(origin, funds, txn);
        _liability_mgr.UpdateLiabilityAmount(funds.liability_hash, funds.amount, txn);
    }
    return consolidated;
}

void StakingManager::Delete(
        ThawingFunds const & funds,
        AccountAddress const & origin,
        MDB_txn* txn)
{
    _store.thawing_del(origin, funds, txn);
}

void StakingManager::Delete(
        StakedFunds const & funds,
        AccountAddress const & origin,
        MDB_txn* txn)
{
    _store.stake_del(origin, txn);
}

bool StakingManager::GetCurrentStakedFunds(
        AccountAddress const & origin,
        StakedFunds& funds,
        MDB_txn* txn)
{
    return !_store.stake_get(origin, funds, txn);
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
    ProcessThawingFunds(origin, make_vec, txn);
    return thawing;
}

void StakingManager::ProcessThawingFunds(
        AccountAddress const & origin,
        std::function<bool(ThawingFunds & funds)> func,
        MDB_txn* txn)
{
    auto func2 = [func](ThawingFunds& funds, logos::store_iterator& it)
    {
        return func(funds);
    };
    ProcessThawingFunds(origin,func2,txn);

}

void StakingManager::ProcessThawingFunds(
        AccountAddress const & origin,
        std::function<bool(ThawingFunds& funds, logos::store_iterator&)> func,
        MDB_txn* txn)
{
    using FuncType = std::function<bool(ThawingFunds& funds, logos::store_iterator&)>;
    //no matter what the func passed as argument is, we never want to iterate
    //through thawing funds that are not owned by origin
    //adding this filter on top of whatever func is avoids repetition in 
    //calling code (otherwise all callers would need to include this filter)
    FuncType filter = [&func,&origin](ThawingFunds& funds, logos::store_iterator& it) -> bool
    {
        //if the key is not origin, stop iteration
        if(it->first.uint256() != origin)
        {
            return false;
        }
        else
        {
            return func(funds, it);
        }
    };
    _store.iterate_db(_store.thawing_db, origin, filter, txn);
}



//modifies cur_stake and account_info
//stores modified cur_stake and newly created ThawingFunds in db
void StakingManager::BeginThawing(
        AccountAddress const & origin,
        logos::account_info& account_info,
        uint32_t epoch,
        StakedFunds& cur_stake,
        Amount amount_to_thaw,
        MDB_txn* txn)
{
    ThawingFunds thawing = CreateThawingFunds(cur_stake.target, origin, epoch, txn);
    LOG_DEBUG(_log) << "amount to thaw = " << amount_to_thaw.to_string()
        << " cur_stake.amount = " << cur_stake.amount.to_string();
    Amount extracted = Extract(cur_stake, thawing, amount_to_thaw, origin, epoch, txn);
    assert(amount_to_thaw == extracted);
    //Don't care about return value here
    Store(thawing, origin, txn);
}

//modifies cur_stake and account_info
//Note, stores modified cur_stake and newly created ThawingFunds in db
void StakingManager::ReduceStake(
        AccountAddress const & origin,
        logos::account_info& account_info,
        uint32_t epoch,
        StakedFunds& cur_stake,
        Amount const & amount_to_thaw,
        MDB_txn* txn)
{
    if(cur_stake.target == origin)
    {
        _voting_power_mgr.SubtractSelfStake(cur_stake.target, amount_to_thaw, epoch, txn); 
    }
    else
    {
        _voting_power_mgr.SubtractLockedProxied(cur_stake.target, amount_to_thaw, epoch, txn);
    }
    BeginThawing(origin, account_info, epoch, cur_stake, amount_to_thaw,txn);
}

//modifies cur_stake, account_info and amount_left
//This function attempts to extract amount_left from cur_stake into new StakedFunds,
//which are returned by value. This function also updates affected
//voting power, and any affected liabilities. 
//If some funds remain in cur_stake after extraction, those
//remaining funds begin thawing, and are stored in the DB
//It is the responsibility of the caller to store the returned value in the DB
//via Store()
//Note, if amount_left is greater than 0 when this function returns, there
//is still more work to be done to satisfy the staking request
//The software will attempt to use thawing funds and then available balance
//This later work will also have an effect on liabilities and voting power,
//which is not handled here, but handled later on in the Stake() member function
//Note, this funtion does not alter the available balance of an account 
StakedFunds StakingManager::ChangeTarget(
        AccountAddress const & origin,
        logos::account_info& account_info,
        uint32_t epoch,
        StakedFunds& cur_stake,
        AccountAddress const & new_target,
        Amount & amount_left,
        MDB_txn* txn)
{

    //Subtract voting power from target
    if(cur_stake.target == origin)
    {
        _voting_power_mgr.SubtractSelfStake(cur_stake.target, cur_stake.amount, epoch, txn);
    }
    else
    {
        _voting_power_mgr.SubtractLockedProxied(cur_stake.target, cur_stake.amount, epoch, txn);
        _voting_power_mgr.SubtractUnlockedProxied(cur_stake.target,account_info.GetAvailableBalance(),epoch,txn);

    }

    StakedFunds new_stake = CreateStakedFunds(new_target, origin, txn);
    //note that amount_left is updated based on amount actually extracted
    amount_left -= Extract(cur_stake, new_stake, amount_left, origin, epoch, txn);

    //Add voting power to new target
    //Note this only adds voting power based on the amount extracted here
    if(new_target == origin)
    {

        _voting_power_mgr.AddSelfStake(new_stake.target, new_stake.amount, epoch, txn);
    }
    else
    { 
        _voting_power_mgr.AddLockedProxied(new_stake.target, new_stake.amount, epoch, txn);

        _voting_power_mgr.AddUnlockedProxied(new_stake.target,account_info.GetAvailableBalance(), epoch, txn);
    }

    //thaw any remaining funds
    if(cur_stake.amount > 0)
    {
        BeginThawing(origin,account_info,epoch,cur_stake,cur_stake.amount,txn);
    }
    return new_stake;
}




//Note, this function sets the rep of account_info based on target
void StakingManager::Stake(
        AccountAddress const & origin,
        logos::account_info & account_info,
        Amount const & amount,
        AccountAddress const & target,
        uint32_t const & epoch,
        MDB_txn* txn)
{
    Amount amount_left = amount;
    StakedFunds cur_stake;

    /*
     * This function iteratively builds StakedFunds with the requested amount
     * amount_left is modified throughout the function, and in called functions,
     * until amount_left is 0.
     */

    bool has_stake = GetCurrentStakedFunds(origin, cur_stake, txn);
    if(!has_stake)
    {
        //Return by value here produces a copy that is not elided
        //TODO refactor to avoid the copy?
        cur_stake = CreateStakedFunds(target, origin, txn);
    }

    _liability_mgr.PruneSecondaryLiabilities(origin, account_info, epoch, txn);

    /* Handle the case where origin is staking to a new target.
     * ChangeTarget() will create any secondary liabilities (if possible)
     * and update voting power of old target and new target (including 
     * unlocked proxy).
     * If amount is less than cur_stake.amount, the remaining amount that was
     * not extracted moves to the thawing state and is stored in the db as
     * ThawingFunds
     * Note the returned value is not stored in the db, as the software may
     * need to extract additional ThawingFunds 
     * or use available funds to stake the amount requested. 
     * This additional work is not done in ChangeTarget()
     * but later on in this function (Stake())
     */
    if(target != cur_stake.target && has_stake)
    {
        //Note, amount_left is passed in by ref and modified
        cur_stake = ChangeTarget(origin,account_info,epoch,cur_stake,target,amount_left,txn);
        //if we are changing target and reducing the amount to 0, delete the
        //StakedFunds record in db and return early
        if(amount == 0)
        {
            Delete(cur_stake, origin, txn);
            return;  
        }

    }
   /* Handle the case where origin is not changing target of stake,
    * and is reducing stake to current target
    * Note the return here. We do not need to touch thawing funds or available
    * funds in this case. ReduceStake stores any created ThawingFunds, as well
    * as the modified cur_stake in db, and updates any associated liabilities
    * in db
    */
    else if(amount_left < cur_stake.amount && has_stake)
    {
        Amount amount_to_thaw = cur_stake.amount - amount_left;
        ReduceStake(origin,account_info,epoch,cur_stake,amount_to_thaw,txn);
        return;
    }
    /* Handle case where origin is not changing target of stake, and is
     * increasing the amount staked to current target. We only set the
     * amount here, as the extraction from ThawingFunds or staking of
     * additional available funds is done below
     */
    else if(has_stake)
    {
        amount_left -= cur_stake.amount;
    }
    /* Handle the case where origin has no current staked funds
     * Note, origin may still have a rep in this case
     */
    else
    {
        //Add unlocked proxied, unless staking to self
        if(target != origin)
        {
            _voting_power_mgr.AddUnlockedProxied(target,account_info.GetAvailableBalance(), epoch, txn);
        }
        //subtract unlocked proxy from old rep, if one exists
        if(account_info.rep != 0)
        {
            _voting_power_mgr.SubtractUnlockedProxied(account_info.rep,account_info.GetAvailableBalance(), epoch, txn);
        }
    }

    //Set rep of account
    //Needs to be done before StakeAvailableFunds is called, else updates
    //to unlocked proxy voting power will be wrong
    //But needs to be done after handling each of the 4 cases above
    account_info.rep = target == origin ? 0 : target;

    
   /* Handle the case where the software needs to use ThawingFunds or
    * additional available funds to satisfy the request.
    * At this point, cur_stake.target == target and but
    * cur_stake.amount is less than amount requested.
    * The software needs more funds to satsify the request
    * First, attempt to stake ThawingFunds
    * Then, use available funds if necessary
    * This code path will be hit when increasing stake to current target,
    * and can also be hit when changing target (if changing target and 
    * increasing stake, or if secondary liabilities prevented the instant
    * redelegation of existing stake)
    */
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

       //Extract from thawing until amount_left is 0
       //Any modified ThawingFunds are stored in db (see Extract())
       //cur_stake is not stored in db
       auto extract_thawing = [&](ThawingFunds & t)
       {
            amount_left -= Extract(t, cur_stake, amount_left, origin, epoch, txn);
            return amount_left > 0;
       }; 
       ProcessThawingFunds(origin, extract_thawing, txn);

       if(amount_left > 0)
       {
           //still need to stake more even after using thawing,
            StakeAvailableFunds(cur_stake, amount_left, origin, account_info, epoch, txn);
       }
    }
    //Finally, store the updated staked funds
    //Note this code path is not hit for the reduce stake to current target case
    if(cur_stake.amount != 0)
    {
        Store(cur_stake, origin, txn);
    }
}

bool StakingManager::Validate(
        AccountAddress const & origin,
        logos::account_info const & info,
        Amount const & amount,
        AccountAddress const & target,
        uint32_t const & epoch,
        Amount const & fee,
        MDB_txn* txn)
{
    if(info.GetAvailableBalance() < fee)
    {
        return false;
    }
    Amount available = info.GetAvailableBalance() - fee;
    //if account has enough available funds, we know request will succeed,
    //even if software uses thawing funds or existing staked funds instead
    if(available >= amount)
    {
        return true;
    }
    else
    {
        //add in any thawing funds that have expired
        available += GetPruneableThawingAmount(origin, info, epoch, txn); 
        if(available >= amount)
        {
            return true;
        }
    }
    //If we get here, there are not enough available funds. need to check if
    //software can use existing StakedFunds (in case of changing target)
    //and/or ThawingFunds


    StakedFunds cur_stake;
    bool has_stake =
        GetCurrentStakedFunds(origin, cur_stake, txn);

    /* Helper lambda function
     * A) _liability_mgr.CanCreateSecondaryLiability is called multiple times
     * within this function, possibly with the same arguments, so results
     * are cached to avoid repeating the same work.
     * B) Any secondary liabilities created must have the same target
     * While two separate calls to 
     * _liability_mgr.CanCreateSecondaryLiability() with different targets 
     * may both return true, the software will be unable to create both of 
     * those secondary liabilities when the request is applied.
     * The map and bool flag make sure that this function is aware of any 
     * possible conflicts between secondary liabilities that will be created
     */
    std::unordered_map<AccountAddress,bool> secondary_liability_cache;
    bool already_created_secondary = false;
    auto can_create_secondary_liability = [&](AccountAddress& liability_target)
    {

        if(secondary_liability_cache.find(liability_target) == secondary_liability_cache.end())
        {
            if(already_created_secondary)
            {
                //all secondary liabilities created must have same target
                return false;
            }
            bool can_create = 
                _liability_mgr.CanCreateSecondaryLiability(liability_target, origin, info, epoch, txn);
            if(can_create)
            {
                already_created_secondary = true;
            }
            secondary_liability_cache[liability_target] = can_create;
            return can_create;
        }
        return secondary_liability_cache[liability_target];
    };




    Amount remaining = amount - available;

    //if not enough available funds, attempt to use existing stake to satisfy
    //remaining portion of request
    if(has_stake && cur_stake.amount > 0)
    {
        if(cur_stake.target == target 
                || can_create_secondary_liability(cur_stake.target))
        {
            if(cur_stake.amount >= remaining)
            {
                return true;
            }
            remaining -= cur_stake.amount;
        }
    }
    bool res = false;
    //if available funds and staked funds together cannot satisfy request,
    //attempt to use thawing funds to satisfy remaining portion of request
    ProcessThawingFunds(origin,[&](ThawingFunds & t)
        {
            if(t.target == target 
                    || can_create_secondary_liability(t.target))
            {
                if(t.amount >= remaining)
                {
                    res = true;
                    return false;   
                }
                remaining -= t.amount;
            }
            return true; 
        }
        ,txn);

    return res;
}


void StakingManager::PruneThawing(
        AccountAddress const & origin,
        logos::account_info & info,
        uint32_t const & cur_epoch,
        MDB_txn* txn)
{
    if(info.epoch_thawing_updated >= cur_epoch)
    {
        return;
    }
    info.epoch_thawing_updated = cur_epoch;

    Amount amount_pruned = 0;

    auto func = [&cur_epoch,&origin,&amount_pruned](ThawingFunds& t, logos::store_iterator& it)
    {
        if(t.expiration_epoch != 0 && t.expiration_epoch <= cur_epoch)
        {
            if(it.delete_current_record())
            {
                Log log;
                LOG_FATAL(log) << "StakingManager::PruneThawing - "
                    << "Error deleting ThawingFunds. origin = " << origin.to_string();
                trace_and_halt();
            }
            amount_pruned += t.amount;
        }
        return true;
    };
    ProcessThawingFunds(origin,func,txn);

    info.SetAvailableBalance(
            info.GetAvailableBalance()+amount_pruned,
            cur_epoch,
            txn);

}

Amount StakingManager::GetPruneableThawingAmount(
        AccountAddress const & origin,
        logos::account_info const & info,
        uint32_t const & cur_epoch,
        MDB_txn* txn)
{
    Amount total = 0;
    if(info.epoch_thawing_updated >= cur_epoch)
    {
        return total;
    }
    auto func = [&cur_epoch,&total](ThawingFunds& t)
    {
        if(t.expiration_epoch != 0 && t.expiration_epoch <= cur_epoch)
        {
            total += t.amount;
        }
        return true;
    };

    ProcessThawingFunds(origin,func,txn);
    return total;
}

void StakingManager::MarkThawingAsFrozen(
        AccountAddress const & origin,
        uint32_t const & epoch_created,
        MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "StakingManager::MarkThawingAsFrozen - "
            << "txn is null";
        trace_and_halt();
    }
    uint32_t epoch_to_mark_frozen = epoch_created+THAWING_PERIOD;
    std::vector<ThawingFunds> updated;
    auto update = [&updated,&txn,&epoch_to_mark_frozen,&origin,this]
        (ThawingFunds& funds, logos::store_iterator& it)
    {
        if(funds.expiration_epoch == epoch_to_mark_frozen
                && funds.target == origin)
        {

            funds.expiration_epoch = 0;
            _liability_mgr.DeleteLiability(funds.liability_hash, txn);
            funds.liability_hash = 
                _liability_mgr.CreateUnexpiringLiability(
                    funds.target,
                    origin,
                    funds.amount,
                    txn);
            //cannot modify records while iterating
            //since modification will change sort order
            updated.push_back(funds);
            if(it.delete_current_record())
            {
                LOG_FATAL(_log) << "StakingManager::MarkThawingAsFrozen - "
                    << "mdb_cursor_del failed. origin = "
                    << origin.to_string();
            }
        }
        //thawing funds are stored in reverse order of expiration_epoch
        //expiration epoch decreases as loop continues
        //can terminate here
        else if(funds.expiration_epoch < epoch_to_mark_frozen)
        {
            return false;
        }
        return true;
    };
    ProcessThawingFunds(origin, update, txn);
    for(auto t : updated)
    {
        Store(t,origin,txn);
    }
}

void StakingManager::SetExpirationOfFrozen(
        AccountAddress const & origin,
        uint32_t const & epoch_unfrozen,
        MDB_txn* txn)
{

    if(!txn)
    {
        LOG_FATAL(_log) << "StakingManager::MarkThawingAsFrozen - "
            << "txn is null";
        trace_and_halt();
    }
    uint32_t exp_epoch = epoch_unfrozen + THAWING_PERIOD;
    std::vector<ThawingFunds> updated;
    auto update = [&exp_epoch,&origin,&updated,&txn,this](ThawingFunds& funds, logos::store_iterator& it)
    {
        //expiration of 0 represents frozen funds
        if(funds.expiration_epoch == 0)
        {
            funds.expiration_epoch = exp_epoch;
            _liability_mgr.DeleteLiability(funds.liability_hash, txn);
            funds.liability_hash = 
                _liability_mgr.CreateExpiringLiability(
                    funds.target,
                    origin,
                    funds.amount,
                    exp_epoch,
                    txn);

            //cannot modify records while iterating
            //since modification will change sort order
            updated.push_back(funds);
            if(it.delete_current_record())
            {
                LOG_FATAL(_log) << "StakingManager::SetExpirationOfFrozen - "
                    << " mdb_cursor_del failed. origin = " 
                    << origin.to_string();
                trace_and_halt();
            }
        }
        return true;
    };
    ProcessThawingFunds(origin, update, txn);
    for(auto t : updated)
    {
        Store(t,origin,txn);
    }
}
