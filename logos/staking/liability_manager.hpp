#pragma once

#include <logos/blockstore.hpp>
#include <logos/staking/liability.hpp>

class LiabilityManager
{

    using BlockStore = logos::block_store;
    public:
    LiabilityManager(BlockStore& store) : _store(store) {}

    LiabilityHash CreateExpiringLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount,
            uint32_t const & expiration_epoch);

    LiabilityHash CreateUnexpiringLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount);

    bool CreateSecondaryLiability(
            AccountAddress const & target,
            AccountAddress const & source,
            Amount const & amount,
            uint32_t const & expiration_epoch);

    void UpdateLiabilityAmount(
            LiabilityHash const & hash,
            Amount const & amount);

    void DeleteLiability(LiabilityHash const & hash);


    private:
    BlockStore& _store;
};
