#include <logos/rewards/epoch_rewards_manager.hpp>


EpochRewardsManager::EpochRewardsManager(BlockStore &store) : _store(store) {}


std::string toString(std::array<uint8_t, EPOCH_REWARDS_KEYSIZE>& arr)
{
    std::string res;
    res.reserve(arr.size());
    std::cout << "arr size is " << arr.size() << std::endl;
    for(size_t i = 0; i < arr.size(); ++i)
    {
        res += std::to_string(arr[i]);
    }
    return res;
}

template <class T, size_t N>
ostream& operator<<(std::ostream& o, const std::array<T, N>& arr)
{
    copy(arr.cbegin(), arr.cend(), ostream_iterator<T>(o, " "));
    return o;
}

std::array<uint8_t, EPOCH_REWARDS_KEYSIZE> EpochRewardsManager::MakeKey(
        AccountAddress const & account,
        uint32_t const & epoch)
{
    std::array<uint8_t,EPOCH_REWARDS_KEYSIZE> key;
    uint8_t const * epoch_bytes = 
        reinterpret_cast<uint8_t const *>(&(epoch));
    for(size_t i = 0; i < EPOCH_REWARDS_KEYSIZE; ++i)
    {
        if(i < 32)
        {
            key[i] = account.data()[i];
        } else
        {
            key[i] = epoch_bytes[i-32];
        }
    }
    return key;
}

void EpochRewardsManager::Init(
        AccountAddress const & rep_address,
        RepEpochInfo const & rep_epoch_info,
        MDB_txn * txn)
{
    if(!txn)
    {
        LOG_FATAL(_log) << "EpochRewardsManager::Init - txn is null";
        trace_and_halt();
    }
    
    auto key = MakeKey(rep_address,rep_epoch_info.epoch_number);
    LOG_INFO(_log) << "EpochRewardsManager::Init - key is " << toString(key);

    EpochRewardsInfo info{
        rep_epoch_info.levy_percentage,
        rep_epoch_info.total_stake,
        0,
        0};

    _store.put(
            _store.epoch_rewards_db,
            logos::mdb_val(key.size(),key.data()),
            info,
            txn); 
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
        << toString(key);
    
    EpochRewardsInfo info = GetEpochRewardsInfo(key,txn);

    info.total_reward = total_reward;
    info.remaining_reward = total_reward;


    _store.put(
            _store.epoch_rewards_db,
            logos::mdb_val(key.size(),key.data()),
            info,
            txn); 

    return false;
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
        << toString(key);

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
    return false;   
}

EpochRewardsInfo EpochRewardsManager::GetEpochRewardsInfo(
        AccountAddress const & rep_address,
        uint32_t const & epoch_number,
        MDB_txn* txn)
{
    auto key = MakeKey(rep_address, epoch_number);
    LOG_INFO(_log) << "EpochRewardsManager::GetEpochRewardsInfo - "
        << "key is " << toString(key);
    return GetEpochRewardsInfo(key,txn);
}


EpochRewardsInfo EpochRewardsManager::GetEpochRewardsInfo(
        std::array<uint8_t, EPOCH_REWARDS_KEYSIZE>& key,
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
            << "failed to get info for key = " << toString(key);
        trace_and_halt();
    }
    return info;
}
