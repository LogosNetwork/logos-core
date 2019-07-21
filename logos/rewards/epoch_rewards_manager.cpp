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
    
    auto key = MakeKey(rep_address, rep_epoch_info.epoch_number);

    RewardsInfo info
    {
        false,
        rep_epoch_info.levy_percentage,
        rep_epoch_info.total_stake,
        rep_epoch_info.self_stake,
        0,
        0
    };

    _store.rewards_put(logos::mdb_val(key.size(), key.data()),
                       info,
                       txn);

    AddGlobalStake(rep_epoch_info, txn);
}

bool EpochRewardsManager::OnFeeCollected(uint32_t epoch_number,
                                         const Amount & value,
                                         MDB_txn * txn)
{
    Amount fee = 0;

    _store.fee_pool_get(logos::mdb_val(epoch_number), fee, txn);

    fee += value;

    return _store.fee_pool_put(logos::mdb_val(epoch_number), fee, txn);
}

bool EpochRewardsManager::GetFeePool(uint32_t epoch_number,
                                     Amount & value,
                                     MDB_txn * txn)
{
    return _store.fee_pool_get(logos::mdb_val(epoch_number), value, txn);
}

bool EpochRewardsManager::RemoveFeePool(uint32_t epoch_number,
                                        MDB_txn * txn)
{
    return _store.fee_pool_remove(logos::mdb_val(epoch_number), txn);
}

bool EpochRewardsManager::SetGlobalReward(uint32_t const & epoch_number,
                                          Amount const & total_reward,
                                          MDB_txn *txn)
{
    auto key = logos::mdb_val(epoch_number);

    auto info = GetGlobalRewardsInfo(epoch_number, txn);

    info.total_reward = total_reward;
    info.remaining_reward = total_reward;

    return _store.global_rewards_put(key, info, txn);
}

bool EpochRewardsManager::HarvestReward(AccountAddress const & rep_address,
                                        uint32_t const & epoch_number,
                                        Amount const & harvest_amount,
                                        RewardsInfo & info,
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

    if(harvest_amount > info.remaining_reward)
    {
        LOG_ERROR(_log) << "EpochRewardsManager::HarvestReward - "
                        << "harvest_amount is greater than remaining_reward";
        return true;
    }

    info.remaining_reward -= harvest_amount;

    auto val = logos::mdb_val(key.size(), key.data());

    if(info.remaining_reward > 0)
    {
        _store.rewards_put(val, info, txn);
    }
    else
    {
        _store.rewards_remove(val, txn);
    }

    return false;
}

void EpochRewardsManager::HarvestGlobalReward(uint32_t const & epoch,
                                              Amount const & to_subtract,
                                              GlobalRewardsInfo global_info,
                                              MDB_txn* txn)
{
    auto key = logos::mdb_val(epoch);

    if(to_subtract > global_info.remaining_reward)
    {
        LOG_FATAL(_log) << "EpochRewardsManager::SubtractGlobalRemainingReward -"
                        << "to_subtract is greater than remaining reward";
        trace_and_halt();
    }

    global_info.remaining_reward -= to_subtract;

    if(global_info.remaining_reward > 0)
    {
        _store.global_rewards_put(key, global_info, txn);
    }
    else
    {
        _store.global_rewards_remove(key, txn);
    }
}

RewardsInfo EpochRewardsManager::GetRewardsInfo(AccountAddress const & rep_address,
                                                uint32_t const & epoch_number,
                                                MDB_txn *txn)
{
    auto key = MakeKey(rep_address, epoch_number);

    LOG_INFO(_log) << "EpochRewardsManager::GetRewardsInfo - "
                   << "key is " << ToString(key);

    return DoGetRewardsInfo(key, txn);
}

RewardsInfo EpochRewardsManager::DoGetRewardsInfo(Key & key,
                                                  MDB_txn *txn)
{
    RewardsInfo info;

    if(_store.rewards_get(logos::mdb_val(key.size(),key.data()), info, txn))
    {
        LOG_FATAL(_log) << "EpochRewardsManager::GetRewardsInfo - "
                        << "failed to get info for key = "
                        << ToString(key);

        trace_and_halt();
    }

    return info;
}

GlobalRewardsInfo EpochRewardsManager::GetGlobalRewardsInfo(uint32_t const & epoch_number,
                                                            MDB_txn *txn)
{
    auto key = logos::mdb_val(epoch_number);

    GlobalRewardsInfo info;

    if(_store.global_rewards_get(key, info, txn))
    {
        LOG_WARN(_log) << "EpochRewardsManager::GetGlobalRewardsInfo - "
                       << "failed to get info for epoch = "
                       << epoch_number;
    }

    return info;
}

bool EpochRewardsManager::RewardsAvailable(AccountAddress const & rep_address,
                                           uint32_t const & epoch_number,
                                           MDB_txn *txn)
{
    auto key = MakeKey(rep_address, epoch_number);

    return _store.rewards_exist(logos::mdb_val(key.size(), key.data()),
                                txn);
}

bool EpochRewardsManager::GlobalRewardsAvailable(uint32_t const & epoch_number,
                                                 MDB_txn* txn)
{
    auto key = logos::mdb_val(epoch_number);

    return _store.global_rewards_exist(key, txn);
}

void EpochRewardsManager::AddGlobalStake(RepEpochInfo const & info,
                                         MDB_txn * txn)
{
    auto key = logos::mdb_val(info.epoch_number);

    auto global_info = GetGlobalRewardsInfo(info.epoch_number, txn);
    global_info.total_stake += info.total_stake;

    _store.global_rewards_put(key, global_info, txn);
}
