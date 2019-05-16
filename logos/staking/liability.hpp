#pragma once

#include <logos/common.hpp>

using LiabilityHash = logos::uint256_union;

/*
 * A liability is a record of source staking to target.
 * For all StakedFunds, there is a liability attached, with an expiration_epoch
 * of 0 (meaning it does not expire)
 * For all ThawingFunds, there is a liability attached, with an expiration_epoch
 * the same as the expiration epoch of the ThawingFunds
 * If an account uses existing StakedFunds or ThawingFunds to satisfy a staking
 * request (staking request is any request that involves staking, including Proxy)
 * and the existing StakedFunds/ThawingFunds have a different target than the request
 * specifies, then a secondary liability is created, in addition to the liability
 * described above. A secondary liability can be loosely thought of as a record of when
 * an account changed their rep. See StakingManager::Extract for more details
 * All non-expired secondary liabilities associated with an account must have the
 * same target, meaning if you reproxy your stake, you must wait one thawing period
 * to reproxy your stake again. Note that you can still submit a proxy request
 * with a new rep,except you will have to use new funds (logos) to stake instead of reproxying
 * existing stake.
 * Liabilities are referenced by their hash, which is a hash of target, source,
 * expiration_epoch and is_secondary. Liabilities that are created that have the
 * same hash will be consolidated together, meaning their amounts will be added
 * together
 * Liabilities themselves are stored in master_liabilities_db, but hashes to the
 * liabilities are stored in rep_liabilities_db, secondary_liabilities_db and
 * are data members of StakedFunds and ThawingFunds, which are stored in staking_db
 * and thawing_db
 * Liabilities are used for slashing, as well as limiting how quickly an account
 * may reproxy their stake
 */
struct Liability
{
    AccountAddress target;
    AccountAddress source;
    Amount amount;
    uint32_t expiration_epoch;
    bool is_secondary;

    LiabilityHash Hash() const;

    void Hash(blake2b_state& hash) const;

    logos::mdb_val to_mdb_val(std::vector<uint8_t>& buf) const
    {
        assert(buf.empty());
        {
            logos::vectorstream stream(buf);
            Serialize(stream);
        }
        return logos::mdb_val(buf.size(), buf.data());
    }

    uint32_t Serialize(logos::stream & stream) const
    {
        uint32_t s = logos::write(stream, target);;
        s += logos::write(stream, source);
        s += logos::write(stream, amount);
        s += logos::write(stream, expiration_epoch);
        s += logos::write(stream, is_secondary);
        return s;
    }

    bool Deserialize(logos::stream & stream)
    {
        return logos::read(stream, target)
            || logos::read(stream, source)
            || logos::read(stream, amount)
            || logos::read(stream, expiration_epoch)
            || logos::read(stream, is_secondary);
    }


};
