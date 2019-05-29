#include <logos/staking/liability_manager.hpp>
#include <logos/staking/staking_manager.hpp>

LiabilityHash LiabilityManager::CreateExpiringLiability(
        AccountAddress const & target,
        AccountAddress const & source,
        Amount const & amount,
        uint32_t const & expiration_epoch,
        MDB_txn* txn)
{
    Liability l{target,source,amount,expiration_epoch,false};
    return Store(l, txn);
}

LiabilityHash LiabilityManager::CreateUnexpiringLiability(
        AccountAddress const & target,
        AccountAddress const & source,
        Amount const & amount,
        MDB_txn* txn)
{
    Liability l{target,source,amount,0,false};
    return Store(l, txn);
}

bool LiabilityManager::CreateSecondaryLiability(
        AccountAddress const & target,
        AccountAddress const & source,
        Amount const & amount,
        uint32_t const & expiration_epoch,
        logos::account_info & info,
        uint32_t const & cur_epoch,
        MDB_txn* txn)
{
    assert(expiration_epoch > cur_epoch);
    Liability l{target,source,amount,expiration_epoch,true};
    //Prune first
    PruneSecondaryLiabilities(source, info, cur_epoch,txn);

    //if can't create this specific secondary liability, return failure
    if(!CanCreateSecondaryLiability(target,source,cur_epoch, txn))
    {
        return false;
    }

    auto hash = Store(l, txn);
    _store.secondary_liability_put(source, hash, txn);
    return true;
}

void LiabilityManager::PruneSecondaryLiabilities(
        AccountAddress const & origin,
        logos::account_info & info,
        uint32_t const & cur_epoch,
        MDB_txn* txn)
{
    if(info.epoch_secondary_liabilities_updated >= cur_epoch)
    {
        return;
    }
    info.epoch_secondary_liabilities_updated = cur_epoch;
    std::vector<LiabilityHash> hashes(GetSecondaryLiabilities(origin, txn));
    for(auto hash : hashes)
    {
        Liability l = Get(hash, txn);
        if(l.expiration_epoch <= cur_epoch)
        {
            _store.secondary_liability_del(hash, txn);
        }
    }
}


//This function returns true if the software is able to create a secondary liability
//with the given arguments
//Currently, all liabilities with the same source must also have the same target
//This function returns false if there are any liabilities with the same source but
//different target. Otherwise, this function returns true
bool LiabilityManager::CanCreateSecondaryLiability(
        AccountAddress const & target,
        AccountAddress const & source,
        uint32_t const & cur_epoch,
        MDB_txn* txn)
{
    //cannot move self stake to lock proxy
    if(target == source)
    {
        return false;
    }

    Liability l{target, source, 0, cur_epoch + THAWING_PERIOD, true};
    auto hash = l.Hash();

    if(Exists(hash, txn))
    {
        //LiabilityHash is a hash of source, target and expiration epoch
        //if a liability with the same hash already exists, we know that existing
        //liability has the same target, so no conflict exists
        return true;
    }

    std::vector<LiabilityHash> hashes(GetSecondaryLiabilities(source, txn));
    for(auto hash : hashes)
    {
        Liability l = Get(hash, txn);

        //if l.expiration_epoch <= cur_epoch, the liability is expired, so skip it
        //otherwise, make sure target is same and return
        //no need to check any other secondary liabilities, since all secondary
        //liabilities for this account will have the same target
        if(l.expiration_epoch > cur_epoch)
        {
            return l.target == target;
        }
    }
    //Execution gets here if source has no secondary liabilities, or all secondary
    //liabilities of source are expired
    return true;
}

void LiabilityManager::UpdateLiabilityAmount(
        LiabilityHash const & hash,
        Amount const & amount,
        MDB_txn* txn)
{
    _store.liability_update_amount(hash, amount, txn);
}

void LiabilityManager::DeleteLiability(
        LiabilityHash const & hash,
        MDB_txn* txn)
{
    _store.liability_del(hash,txn);

}

LiabilityHash LiabilityManager::Store(Liability const & l, MDB_txn* txn)
{
    LiabilityHash hash = l.Hash();
    _store.liability_put(hash, l,txn);
    return hash;
}

std::vector<LiabilityHash> LiabilityManager::GetHashes(
        AccountAddress const & account,
        MDB_dbi BlockStore::*dbi,
        MDB_txn* txn)
{
    std::vector<LiabilityHash> hashes;

    for(auto it = logos::store_iterator(txn,_store.*dbi, logos::mdb_val(account));
            it != logos::store_iterator(nullptr) && it->first.uint256() == account; ++it)
    {
        hashes.push_back(it->second.uint256());
    }
    return hashes;
}

std::vector<LiabilityHash> LiabilityManager::GetSecondaryLiabilities(
        AccountAddress const & origin,
        MDB_txn* txn)
{
    return GetHashes(origin,&BlockStore::secondary_liabilities_db,txn);
}

std::vector<LiabilityHash> LiabilityManager::GetRepLiabilities(
        AccountAddress const & rep,
        MDB_txn* txn)
{
    return GetHashes(rep,&BlockStore::rep_liabilities_db,txn);
}

Liability LiabilityManager::Get(LiabilityHash const & hash, MDB_txn* txn)
{
    Liability l;
    if(_store.liability_get(hash, l, txn))
    {
        LOG_FATAL(_log) << "LiabilityManager::Get - liability does not exist "
            << ". hash = " << hash.to_string();
        trace_and_halt();
    }
    return l;
}

bool LiabilityManager::Exists(LiabilityHash const & hash, MDB_txn* txn)
{
    return _store.liability_exists(hash, txn);
}
