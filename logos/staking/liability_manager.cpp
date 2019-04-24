#include <logos/staking/liability_manager.hpp>

LiabilityHash LiabilityManager::CreateExpiringLiability(
        AccountAddress const & target,
        AccountAddress const & source,
        Amount const & amount,
        uint32_t const & expiration_epoch)
{
    return 0;
}

LiabilityHash LiabilityManager::CreateUnexpiringLiability(
        AccountAddress const & target,
        AccountAddress const & source,
        Amount const & amount)
{
    return 0;
}

bool LiabilityManager::CreateSecondaryLiability(
        AccountAddress const & target,
        AccountAddress const & source,
        Amount const & amount,
        uint32_t const & expiration_epoch)

{
    return true;
}

void LiabilityManager::UpdateLiabilityAmount(
        LiabilityHash const & hash,
        Amount const & amount)
{}

void LiabilityManager::DeleteLiability(LiabilityHash const & hash)
{}
