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
        MDB_txn* txn)
{
    Liability l{target,source,amount,expiration_epoch,true};
    //using dummy info here, and passing dummy.epoch_secondary_liabilities_updated
    //as the epoch argument ensures only the first liability is checked
    //this is fine, as PruneSecondaryLiabilities() is called prior to any
    //CreateSecondaryLiability() calls
    logos::account_info dummy;
    if(!CanCreateSecondaryLiability(target,source,dummy,dummy.epoch_secondary_liabilities_updated, txn))
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


bool LiabilityManager::CanCreateSecondaryLiability(
        AccountAddress const & target,
        AccountAddress const & source,
        logos::account_info const & info,
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
        //consolidation will occur, no way for this to fail
        return true;
    }

    std::vector<LiabilityHash> hashes(GetSecondaryLiabilities(source, txn));
    for(auto hash : hashes)
    {
        Liability l = Get(hash, txn);
        if(info.epoch_secondary_liabilities_updated >= cur_epoch)
        {
            //secondary liabilities are all up to date
            //only need to check first
            //Possible optimization: don't get all hashes in this case
            return l.target == target;
        }
        else if(l.expiration_epoch > cur_epoch)
        {
            return l.target == target;
        }
    }
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
    _store.liability_put(l,txn);
    return l.Hash();
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
