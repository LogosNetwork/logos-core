#include <logos/rewards/epoch_rewards_manager.hpp>

std::shared_ptr<EpochRewardsManager> EpochRewardsManager::instance = nullptr;

std::string ToString(EpochRewardsManager::Key & key)
{
    std::string res;
    res.reserve(key.size());

    std::for_each(key.begin(), key.end(),
                  [&res](const auto & val)
                  {
                      res += std::to_string(val);
                  });

    return res;
}

template <class T, size_t N>
ostream& operator<<(std::ostream& o, const std::array<T, N>& arr)
{
    copy(arr.cbegin(), arr.cend(), ostream_iterator<T>(o, " "));
    return o;
}

EpochRewardsManager::EpochRewardsManager(BlockStore & store)
    : _store(store)
{}

auto EpochRewardsManager::MakeKey(AccountAddress const & account,
                                  const uint32_t & epoch) -> Key
{
    Key key;

    std::memcpy(key.data(), account.data(), sizeof(account.bytes));
    std::memcpy(key.data() + sizeof(account.bytes), reinterpret_cast<const uint8_t *>(&epoch), sizeof(epoch));

    return key;
}

void EpochRewardsManager::Init(AccountAddress const & rep_address,
                               RepEpochInfo const & rep_epoch_info,
                               MDB_txn * txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "EpochRewardsManager::Init - txn is null";
        trace_and_halt();
    }
    
    auto key = MakeKey(rep_address,rep_epoch_info.epoch_number);

    EpochRewardsInfo info
    {
        false,
        rep_epoch_info.levy_percentage,
        rep_epoch_info.total_stake,
        rep_epoch_info.self_stake,
        0,
        0
    };

    _store.put(
            _store.epoch_rewards_db,
            logos::mdb_val(key.size(),key.data()),
            info,
            txn);

    AddGlobalStake(rep_epoch_info, txn);
}

bool EpochRewardsManager::SetTotalReward(
        AccountAddress const & rep_address,
        uint32_t const & epoch_number,
        Amount const & total_reward,
        MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "EpochRewardsManager::SetTotalReward - txn is null";
        trace_and_halt();
    }

    auto key = MakeKey(rep_address, epoch_number);

    LOG_INFO(_log) << "EpochRewardsManager::SetTotalReward - key is " 
                   << ToString(key);
    
    EpochRewardsInfo info = GetEpochRewardsInfo(key,txn);

    info.total_reward = total_reward;
    info.remaining_reward = total_reward;

    _store.put(
            _store.epoch_rewards_db,
            logos::mdb_val(key.size(),key.data()),
            info,
            txn);

    // TODO: set global total first, then infer rep total
    AddGlobalTotalReward(epoch_number,total_reward,txn);

    return false;
}

bool EpochRewardsManager::SetTotalGlobalReward(
    uint32_t const & epoch_number,
    Amount const & total_reward,
    MDB_txn* txn)
{
    auto key = logos::mdb_val(sizeof(epoch_number),
                              const_cast<uint32_t *>(&epoch_number));

    auto info = GetGlobalEpochRewardsInfo(epoch_number, txn);

    info.total_reward = total_reward;
    info.remaining_reward = total_reward;

    _store.put(_store.global_epoch_rewards_db, key, info, txn);
}

//TODO key is computed twice, only compute once
bool EpochRewardsManager::HarvestReward(
        AccountAddress const & rep_address,
        uint32_t const & epoch_number,
        Amount const & harvest_amount,
        MDB_txn* txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "EpochRewardsManager::HarvestReward - txn is null";
        trace_and_halt();
    }

    auto key = MakeKey(rep_address, epoch_number);
    LOG_INFO(_log) << "EpochRewardsManager::SetTotalReward - key is " 
                   << ToString(key);

    EpochRewardsInfo info = GetEpochRewardsInfo(key,txn);

    if(harvest_amount > info.remaining_reward)
    {
        LOG_ERROR(_log) << "EpochRewardsManager::HarvestReward - "
                        << "harvest_amount is greater than remaining_reward";
        return true;
    }
    info.remaining_reward -= harvest_amount;


    if(info.remaining_reward > 0)
    {
        _store.put(
                _store.epoch_rewards_db,
                logos::mdb_val(key.size(),key.data()),
                info,
                txn); 
    }
    else
    {
        _store.del(
                _store.epoch_rewards_db,
                logos::mdb_val(key.size(),key.data()),
                txn);
    }
    SubtractGlobalRemainingReward(epoch_number,harvest_amount,txn);
    return false;   
}

EpochRewardsInfo EpochRewardsManager::GetEpochRewardsInfo(
        AccountAddress const & rep_address,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    auto key = MakeKey(rep_address, epoch_number);

    LOG_INFO(_log) << "EpochRewardsManager::GetEpochRewardsInfo - "
                   << "key is " << ToString(key);

    return GetEpochRewardsInfo(key,txn);
}

void EpochRewardsManager::RemoveEpochRewardsInfo(
    AccountAddress const & rep_address,
    uint32_t const & epoch_number,
    MDB_txn* txn)
{
    auto key = MakeKey(rep_address, epoch_number);

    LOG_INFO(_log) << "EpochRewardsManager::RemoveEpochRewardsInfo - "
                   << "key is " << ToString(key);

    if(_store.del(_store.epoch_rewards_db,
                  logos::mdb_val(key.size(),key.data()),
                  txn))
    {
        LOG_FATAL(_log) << "EpochRewardsManager::RemoveEpochRewardsInfo - "
                        << "failed to remove rewards for key = "
                        << ToString(key);

        trace_and_halt();
    }
}

EpochRewardsInfo EpochRewardsManager::GetEpochRewardsInfo(Key & key,
                                                          MDB_txn* txn)
{
    EpochRewardsInfo info;

    if(_store.get(
                _store.epoch_rewards_db
                ,logos::mdb_val(key.size(),key.data())
                ,info
                ,txn))
    {
        LOG_FATAL(_log) << "EpochRewardsManager::GetEpochRewardsInfo - "
                        << "failed to get info for key = " << ToString(key);

        trace_and_halt();
    }

    return info;
}

GlobalEpochRewardsInfo EpochRewardsManager::GetGlobalEpochRewardsInfo(
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    auto key = logos::mdb_val(sizeof(epoch_number),
                              const_cast<uint32_t *>(&epoch_number));

    GlobalEpochRewardsInfo info;

    if(_store.get(_store.global_epoch_rewards_db, key, info, txn))
    {
        LOG_WARN(_log) << "EpochRewardsManager::GetGlobalEpochRewardsInfo - "
                       << "failed to get info for epoch = "
                       << epoch_number;
    }

    return info;
}

void EpochRewardsManager::RemoveGlobalRewards(uint32_t const & epoch_number,
                                              MDB_txn* txn)
{
    auto key = logos::mdb_val(sizeof(epoch_number),
                              const_cast<uint32_t *>(&epoch_number));

    if(_store.del(_store.global_epoch_rewards_db, key, txn))
    {
        LOG_FATAL(_log) << "EpochRewardsManager::RemoveGlobalRewards - "
                        << "failed to remove global rewards for epoch = "
                        << epoch_number;

        trace_and_halt();
    }
}

bool EpochRewardsManager::HasRewards(AccountAddress const & rep_address,
                                     uint32_t const & epoch_number,
                                     MDB_txn* txn)
{
    auto key = MakeKey(rep_address, epoch_number);

    return _store.rep_rewards_exist(logos::mdb_val(key.size(),key.data()),
                                    txn);
}

bool EpochRewardsManager::GlobalRewardsAvailable(uint32_t const & epoch_number,
                                                 MDB_txn* txn)
{
    auto key = logos::mdb_val(sizeof(epoch_number),
                              const_cast<uint32_t *>(&epoch_number));

    return _store.global_rewards_exist(logos::mdb_val(key.size(),key.data()),
                                       txn);
}

void EpochRewardsManager::AddGlobalStake(
        RepEpochInfo const & info,
        MDB_txn* txn)
{
    auto key = logos::mdb_val(
            sizeof(info.epoch_number),
            const_cast<uint32_t *>(&(info.epoch_number)));

    GlobalEpochRewardsInfo global_info(GetGlobalEpochRewardsInfo(info.epoch_number, txn));
    global_info.total_stake += info.total_stake;
    _store.put(_store.global_epoch_rewards_db,key,global_info,txn);
}

void EpochRewardsManager::AddGlobalTotalReward(
        uint32_t const & epoch,
        Amount const & to_add,
        MDB_txn* txn)
{
    auto key = logos::mdb_val(
            sizeof(epoch),
            const_cast<uint32_t *>(&epoch));

    GlobalEpochRewardsInfo global_info(GetGlobalEpochRewardsInfo(epoch, txn));
    global_info.total_reward += to_add;
    global_info.remaining_reward += to_add;
    _store.put(_store.global_epoch_rewards_db,key,global_info,txn);

}

void EpochRewardsManager::SubtractGlobalRemainingReward(
        uint32_t const & epoch,
        Amount const & to_subtract,
        MDB_txn* txn)
{
    auto key = logos::mdb_val(
            sizeof(epoch),
            const_cast<uint32_t *>(&epoch));

    GlobalEpochRewardsInfo global_info(GetGlobalEpochRewardsInfo(epoch, txn));
    if(to_subtract > global_info.remaining_reward)
    {
        LOG_FATAL(_log) << "EpochRewardsManager::SubtractGlobalRemainingReward -"
            << "to_subtract is greater than remaining reward";
        trace_and_halt();
    }
    global_info.remaining_reward -= to_subtract;
    if(global_info.remaining_reward > 0)
    {
        _store.put(_store.global_epoch_rewards_db,key,global_info,txn);
    }
    else
    {
        _store.del(_store.global_epoch_rewards_db,key,txn);
    }
}
