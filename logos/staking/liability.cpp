#include <logos/staking/liability.hpp>

LiabilityHash Liability::Hash() const
{
    return Blake2bHash(*this);

}

void Liability::Hash(blake2b_state& hash) const
{
    blake2b_update(&hash, &target, sizeof(target));
    blake2b_update(&hash, &source, sizeof(source));
    blake2b_update(&hash, &expiration_epoch, sizeof(expiration_epoch));
    blake2b_update(&hash, &is_secondary, sizeof(is_secondary));
}
